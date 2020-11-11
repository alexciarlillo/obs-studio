#include "mediasoup-encoder.h"

#include "util/config-file.h"
#include "util/darray.h"
#include "obs-avc.h"

#include "api/video_codecs/video_encoder_factory.h"
#include "webrtc/media/base/h264_profile_level_id.h"
#include "webrtc/rtc_base/timeutils.h"
#include "webrtc/modules/video_coding/include/video_codec_interface.h"
#include "webrtc/modules/include/module_common_types.h"
#include "webrtc/common_video/h264/h264_common.h"
#include "rtc_base/checks.h"

#define SIMPLE_ENCODER_X264_LOWCPU             "x264_lowcpu"
#define SIMPLE_ENCODER_QSV                     "qsv"
#define SIMPLE_ENCODER_NVENC                   "nvenc"
#define SIMPLE_ENCODER_AMD                     "amd"

const size_t kFrameDiffThresholdMs = 350;
const int kMinKeyFrameInterval = 6;

MediasoupVideoEncoder::MediasoupVideoEncoder()
    : clock_(webrtc::Clock::GetRealTimeClock()),
      delta_ntp_internal_ms_(clock_->CurrentNtpInMilliseconds() - clock_->TimeInMilliseconds()) {

}

MediasoupVideoEncoder::~MediasoupVideoEncoder() {

}

bool MediasoupVideoEncoder::open(const ConfigFile& config) {
    const char *encoder_name = config_get_string(config, "SimpleOutput", "StreamEncoder");
    if (encoder_name == NULL) {
        return false;
    }

    //Search and load encoder.
    if (strcmp(encoder_name, SIMPLE_ENCODER_QSV) == 0) {
        load_streaming_preset_h264("obs_qsv11");
    } else if (strcmp(encoder_name, SIMPLE_ENCODER_AMD) == 0) {
        load_streaming_preset_h264("amd_amf_h264");
    } else if (strcmp(encoder_name, SIMPLE_ENCODER_NVENC) == 0) {
        load_streaming_preset_h264("ffmpeg_nvenc");
    } else {
        load_streaming_preset_h264("obs_x264");
    }

    //Configure encoder.
    configure_encoder(config);

    //Initialize encoder.
    if (!obs_encoder_initialize(h264_encoder_)) {
        return false;
    }

    opened = true;
    return true;
}

bool MediasoupVideoEncoder::close() {
    if (h264_encoder_ != NULL) {
        obs_encoder_release(h264_encoder_);
    }
    opened = false;
    return true;
}

bool MediasoupVideoEncoder::start() {
    bool ret = true;
    do {
        if (h264_encoder_ == NULL) {
            ret = false;
            break;
        }

        obs_encoder_t *encoder = h264_encoder_;
        pthread_mutex_lock(&encoder->init_mutex);
        ret = start_internal(encoder);
        pthread_mutex_unlock(&encoder->init_mutex);
    } while (0);
    return ret;
}

bool MediasoupVideoEncoder::stop() {
    bool ret = true;
    do {
        if (h264_encoder_ == NULL) {
            ret = false;
            break;
        }

        obs_encoder_t *encoder = h264_encoder_;
        pthread_mutex_lock(&encoder->init_mutex);
        bool destroyed = stop_internal(encoder, new_encoded_packet, this);
        if (!destroyed) {
            pthread_mutex_unlock(&encoder->init_mutex);
        }
    } while (0);
    return ret;
}

//WebRTC interfaces.
int32_t MediasoupVideoEncoder::InitEncode(
    const webrtc::VideoCodec* codec_settings,
    int32_t number_of_cores,
    size_t max_payload_size) {

    io_service_.reset(new IOService);
    io_service_->start();

    int32_t ret = 0;
    std::shared_ptr<std::promise<int32_t> > promise(new std::promise<int32_t>);
    io_service_->ios()->post(
        boost::bind(&MediasoupVideoEncoder::InitEncodeOnCodecThread,
            this,
            codec_settings->width,
            codec_settings->height,
            codec_settings->targetBitrate,
            codec_settings->maxFramerate,
            promise));
    std::future<int32_t> future = promise->get_future();
    ret = future.get();
    return ret;
}

int32_t MediasoupVideoEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
    std::shared_ptr<std::promise<int32_t> > promise(new std::promise<int32_t>);
    io_service_->ios()->post(
        boost::bind(&MediasoupVideoEncoder::RegisterEncodeCompleteCallbackOnCodecThread,
            this,
            callback,
            promise));
    std::future<int32_t> future = promise->get_future();
    int32_t ret = future.get();
    return ret;
}

int32_t MediasoupVideoEncoder::Release() {
    std::shared_ptr<std::promise<int32_t> > promise(new std::promise<int32_t>);
    io_service_->ios()->post(
        boost::bind(&MediasoupVideoEncoder::ReleaseOnCodecThread,
            this,
            promise));
    std::future<int32_t> future = promise->get_future();
    int32_t ret = future.get();

    if (io_service_ != NULL) {
        io_service_->stop();
        io_service_.reset();
    }
    return ret;
}

int32_t MediasoupVideoEncoder::Encode(
    const webrtc::VideoFrame& frame,
    const webrtc::CodecSpecificInfo* codec_specific_info,
    const std::vector<webrtc::FrameType>* frame_types) {
    //blog(LOG_INFO, "@@@ Encode");
    if (last_encode_error) {
        return -1;
    }

    io_service_->ios()->post(
        boost::bind(&MediasoupVideoEncoder::EncodeOnCodecThread,
            this,
            frame,
            frame_types->front(),
            rtc::TimeMillis()));
    return 0;
}

int32_t MediasoupVideoEncoder::SetChannelParameters(
    uint32_t packet_loss,
    int64_t rtt) {
    return 0;
}

bool MediasoupVideoEncoder::start_internal(obs_encoder_t *encoder) {
    struct encoder_callback cb = { false, new_encoded_packet, this };
    bool first = false;

    if (!encoder->context.data) {
        return false;
    }

    pthread_mutex_lock(&encoder->callbacks_mutex);

    first = (encoder->callbacks.num == 0);

    size_t idx = get_callback_idx(encoder, new_encoded_packet, this);
    if (idx == DARRAY_INVALID) {
        da_push_back(encoder->callbacks, &cb);
    }

    pthread_mutex_unlock(&encoder->callbacks_mutex);

    if (first) {
        encoder->cur_pts = 0;
        os_atomic_set_bool(&encoder->active, true);
    }

    started = true;
    return true;
}

bool MediasoupVideoEncoder::stop_internal(
    obs_encoder_t *encoder,
    void(*new_packet)(void *param, struct encoder_packet *packet),
    void *param) {
    bool   last = false;
    size_t idx;

    pthread_mutex_lock(&encoder->callbacks_mutex);

    idx = get_callback_idx(encoder, new_packet, param);
    if (idx != DARRAY_INVALID) {
        da_erase(encoder->callbacks, idx);
        last = (encoder->callbacks.num == 0);
    }

    pthread_mutex_unlock(&encoder->callbacks_mutex);
    started = false;

    if (last) {
        obs_encoder_shutdown(encoder);
        os_atomic_set_bool(&encoder->active, false);
        encoder->initialized = false;

        if (encoder->destroy_on_stop) {
            pthread_mutex_unlock(&encoder->init_mutex);
            obs_encoder_actually_destroy(encoder);
            return true;
        }
    }

    return false;
}

void MediasoupVideoEncoder::configure_encoder(const ConfigFile& config) {
    obs_data_t *h264Settings = obs_data_create();

    int videoBitrate = config_get_uint(config, "SimpleOutput", "VBitrate");
    bool advanced = config_get_bool(config, "SimpleOutput", "UseAdvanced");
    bool enforceBitrate = config_get_bool(config, "SimpleOutput", "EnforceBitrate");
    const char *custom = config_get_string(config, "SimpleOutput", "x264Settings");
    const char *encoder = config_get_string(config, "SimpleOutput", "StreamEncoder");
    const char *presetType = NULL;
    const char *preset = NULL;

    if (strcmp(encoder, SIMPLE_ENCODER_QSV) == 0) {
        presetType = "QSVPreset";
    } else if (strcmp(encoder, SIMPLE_ENCODER_AMD) == 0) {
        presetType = "AMDPreset";
        update_streaming_settings_amd(h264Settings, videoBitrate);
    } else if (strcmp(encoder, SIMPLE_ENCODER_NVENC) == 0) {
        presetType = "NVENCPreset";
    } else {
        presetType = "Preset";
    }

    preset = config_get_string(config, "SimpleOutput", presetType);

    obs_data_set_string(h264Settings, "rate_control", "CBR");
    obs_data_set_int(h264Settings, "bitrate", videoBitrate);

    //Add by HeZhen, disable b-frames
    obs_data_set_int(h264Settings, "bf", 0);

    //Add by HeZhen, default 250 frames' time.
    obs_data_set_int(h264Settings, "keyint_sec", 10);

  //Add by HeZhen for QSV immediately outputing first frame.
    obs_data_set_int(h264Settings, "async_depth", 1);

    if (advanced) {
        obs_data_set_string(h264Settings, "preset", preset);
        obs_data_set_string(h264Settings, "x264opts", custom);
    }

    if (advanced && !enforceBitrate) {
        obs_data_set_int(h264Settings, "bitrate", videoBitrate);
    }

    video_t *video = obs_get_video();
    enum video_format format = video_output_get_format(video);

    if (format != VIDEO_FORMAT_NV12 && format != VIDEO_FORMAT_I420) {
        obs_encoder_set_preferred_video_format(h264_encoder_, VIDEO_FORMAT_NV12);
    }

    obs_encoder_update(h264_encoder_, h264Settings);
    obs_data_release(h264Settings);
    obs_encoder_set_video(h264_encoder_, video);
}

void MediasoupVideoEncoder::new_encoded_packet(void *param, struct encoder_packet *packet) {
    MediasoupVideoEncoder *encoder = (MediasoupVideoEncoder*)param;
    if (encoder != NULL) {
        encoder->on_new_encoded_packet(packet);
    }
}

void MediasoupVideoEncoder::on_new_encoded_packet(struct encoder_packet *packet) {
    if (!input_frame_infos_.empty()) {
        const InputFrameInfo& frame_info = input_frame_infos_.front();
        output_timestamp_ = frame_info.frame_timestamp;
        output_render_time_ms_ = frame_info.frame_render_time_ms;
        output_rotation_ = frame_info.rotation;
        input_frame_infos_.pop_front();
    }

    int64_t render_time_ms = packet->dts_usec / rtc::kNumMicrosecsPerMillisec;

    // Local time in webrtc time base.
    int64_t current_time_us = clock_->TimeInMicroseconds();
    int64_t current_time_ms = current_time_us / rtc::kNumMicrosecsPerMillisec;
    // In some cases, e.g., when the frame from decoder is fed to encoder,
    // the timestamp may be set to the future. As the encoding pipeline assumes
    // capture time to be less than present time, we should reset the capture
    // timestamps here. Otherwise there may be issues with RTP send stream.
    if (packet->dts_usec > current_time_us) {
        packet->dts_usec = current_time_us;
    }

    // Capture time may come from clock with an offset and drift from clock_.
    int64_t capture_ntp_time_ms;
    if (render_time_ms != 0) {
        capture_ntp_time_ms = render_time_ms + delta_ntp_internal_ms_;
    } else {
        capture_ntp_time_ms = current_time_ms + delta_ntp_internal_ms_;
    }

    // Convert NTP time, in ms, to RTP timestamp.
    const int kMsToRtpTimestamp = 90;
    uint32_t timeStamp = kMsToRtpTimestamp * static_cast<uint32_t>(capture_ntp_time_ms);

    // Extract payload.
    size_t payload_size = packet->size;
    uint8_t* payload = packet->data;

    const webrtc::VideoCodecType codec_type = webrtc::kVideoCodecH264;
    webrtc::EncodedImageCallback::Result callback_result(webrtc::EncodedImageCallback::Result::OK);
    if (callback_) {
        std::unique_ptr<webrtc::EncodedImage> image(new webrtc::EncodedImage(payload, payload_size, payload_size));
        image->_encodedWidth = width_;
        image->_encodedHeight = height_;
        //image->capture_time_ms_ = packet->dts_usec / 1000;
        //image->_timeStamp = image->capture_time_ms_ * 90;
        image->capture_time_ms_ = render_time_ms;
        image->_timeStamp = timeStamp;

        image->rotation_ = output_rotation_;
        image->_frameType = (packet->keyframe ? webrtc::kVideoFrameKey : webrtc::kVideoFrameDelta);
        image->_completeFrame = true;
        webrtc::CodecSpecificInfo info;
        memset(&info, 0, sizeof(info));
        info.codecType = codec_type;

        if (packet->keyframe) {
            blog(LOG_INFO, "Key frame");
        }

        // Generate a header describing a single fragment.
        webrtc::RTPFragmentationHeader header;
        memset(&header, 0, sizeof(header));
        h264_bitstream_parser_.ParseBitstream(payload, payload_size);
        int qp;
        if (h264_bitstream_parser_.GetLastSliceQp(&qp)) {
            //current_acc_qp_ += qp;
            image->qp_ = qp;
        }
        // For H.264 search for start codes.
        const std::vector<webrtc::H264::NaluIndex> nalu_idxs = webrtc::H264::FindNaluIndices(payload, payload_size);
        if (nalu_idxs.empty()) {
            blog(LOG_ERROR, "Start code is not found!");
            blog(LOG_ERROR,
                "Data: %02x %02x %02x %02x %02x %02x",
                image->_buffer[0],
                image->_buffer[1],
                image->_buffer[2],
                image->_buffer[3],
                image->_buffer[4],
                image->_buffer[5]);
            //ProcessHWErrorOnCodecThread(true /* reset_if_fallback_unavailable */);
            //return false;
            return;
        }
        header.VerifyAndAllocateFragmentationHeader(nalu_idxs.size());
        for (size_t i = 0; i < nalu_idxs.size(); i++) {
            header.fragmentationOffset[i] = nalu_idxs[i].payload_start_offset;
            header.fragmentationLength[i] = nalu_idxs[i].payload_size;
            header.fragmentationPlType[i] = 0;
            header.fragmentationTimeDiff[i] = 0;
        }

        callback_result = callback_->OnEncodedImage(*image, &info, &header);
    }
}

void MediasoupVideoEncoder::load_streaming_preset_h264(const char *encoderId) {
    h264_encoder_ = obs_video_encoder_create(encoderId, "bee_h264_stream", nullptr, nullptr);
    if (!h264_encoder_) {
        throw "Failed to create h264 streaming encoder (simple output)";
    }
    obs_encoder_release(h264_encoder_);
}

void MediasoupVideoEncoder::update_streaming_settings_amd(obs_data_t *settings, int bitrate) {
    // Static Properties
    obs_data_set_int(settings, "Usage", 0);
    obs_data_set_int(settings, "Profile", 100); // High

                                                // Rate Control Properties
    obs_data_set_int(settings, "RateControlMethod", 3);
    obs_data_set_int(settings, "Bitrate.Target", bitrate);
    obs_data_set_int(settings, "FillerData", 1);
    obs_data_set_int(settings, "VBVBuffer", 1);
    obs_data_set_int(settings, "VBVBuffer.Size", bitrate);

    // Picture Control Properties
    obs_data_set_double(settings, "KeyframeInterval", 2.0);
    obs_data_set_int(settings, "BFrame.Pattern", 0);
}

size_t MediasoupVideoEncoder::get_callback_idx(
    const struct obs_encoder *encoder,
    void(*new_packet)(void *param, struct encoder_packet *packet),
    void *param) {
    for (size_t i = 0; i < encoder->callbacks.num; i++) {
        struct encoder_callback *cb = encoder->callbacks.array + i;

        if (cb->new_packet == new_packet && cb->param == param)
            return i;
    }

    return DARRAY_INVALID;
}

void MediasoupVideoEncoder::obs_encoder_actually_destroy(obs_encoder_t *encoder) {
    if (encoder) {
        blog(LOG_DEBUG, "encoder '%s' destroyed", encoder->context.name);
        free_audio_buffers(encoder);

        if (encoder->context.data)
            encoder->info.destroy(encoder->context.data);
        da_free(encoder->callbacks);
        pthread_mutex_destroy(&encoder->init_mutex);
        pthread_mutex_destroy(&encoder->callbacks_mutex);
        obs_context_data_free(&encoder->context);
        if (encoder->owns_info_id)
            bfree((void*)encoder->info.id);
        bfree(encoder);
    }
}

void MediasoupVideoEncoder::free_audio_buffers(struct obs_encoder *encoder) {
    for (size_t i = 0; i < MAX_AV_PLANES; i++) {
        circlebuf_free(&encoder->audio_input_buffer[i]);
        bfree(encoder->audio_output_buffer[i]);
        encoder->audio_output_buffer[i] = NULL;
    }
}

void MediasoupVideoEncoder::InitEncodeOnCodecThread(
    int32_t width,
    int32_t height,
    int32_t target_bitrate,
    int32_t fps,
    std::shared_ptr<std::promise<int32_t> > promise) {
    int32_t ret = 0;
    do {
        width_ = width;
        height_ = height;
        target_bitrate_ = target_bitrate;
        fps_ = fps;

        input_frame_infos_.clear();

        const ConfigFile &basic_config = BeeProxy::instance()->get_basic_configure();
        if (!open(basic_config)) {
            ret = -1;
            break;
        }

        if (!start()) {
            ret = -1;
            break;
        }

        BeeProxy::instance()->set_video_encoder(this);
    } while (0);

    if (promise != NULL) {
        promise->set_value(ret);
    }
}

void MediasoupVideoEncoder::RegisterEncodeCompleteCallbackOnCodecThread(
    webrtc::EncodedImageCallback* callback,
    std::shared_ptr<std::promise<int32_t> > promise) {
    callback_ = callback;
    if (promise != NULL) {
        promise->set_value(0);
    }
}

void MediasoupVideoEncoder::ReleaseOnCodecThread(std::shared_ptr<std::promise<int32_t> > promise) {
    BeeProxy::instance()->set_video_encoder(NULL);

    if (started) {
        stop();
    }

    if (opened) {
        close();
    }
    if (promise != NULL) {
        promise->set_value(0);
    }
}

void MediasoupVideoEncoder::EncodeOnCodecThread(
    const webrtc::VideoFrame& frame,
    const webrtc::FrameType frame_type,
    const int64_t frame_input_time_ms) {
    if (frame_type != webrtc::kVideoFrameDelta) {
        send_key_frame_ = true;
    }

    //Just store timestamp, real encoding is bypassed.
    input_frame_infos_.emplace_back(
        frame_input_time_ms,
        frame.timestamp(),
        frame.render_time_ms(),
        frame.rotation());
}

static const char *receive_video_name = "receive_video";
void MediasoupVideoEncoder::obs_encode(
    struct video_data *frame) {
    //blog(LOG_INFO, "@@@ obs_encode");
    profile_start(receive_video_name);

    do {
        struct obs_encoder    *encoder = h264_encoder_;
        struct obs_encoder    *pair = encoder->paired_encoder;
        struct encoder_frame  enc_frame;

        if (!encoder->first_received && pair) {
            if (!pair->first_received ||
                pair->first_raw_ts > frame->timestamp) {
                break;
            }
        }

        memset(&enc_frame, 0, sizeof(struct encoder_frame));

        for (size_t i = 0; i < MAX_AV_PLANES; i++) {
            enc_frame.data[i] = frame->data[i];
            enc_frame.linesize[i] = frame->linesize[i];
        }

        if (!encoder->start_ts) {
            encoder->start_ts = frame->timestamp;
        }

        enc_frame.frames = 1;
        enc_frame.pts = encoder->cur_pts;

        std::shared_ptr<std::promise<int32_t> > promise(new std::promise<int32_t>);
        io_service_->ios()->post(boost::bind(&MediasoupVideoEncoder::do_encode, this, encoder, &enc_frame, promise));
        std::future<int32_t> future = promise->get_future();
        future.get();

        encoder->cur_pts += encoder->timebase_num;
    } while (0);

    profile_end(receive_video_name);
}

static const char *do_encode_name = "do_encode";
void MediasoupVideoEncoder::do_encode(
    struct obs_encoder *encoder,
    struct encoder_frame *frame,
    std::shared_ptr<std::promise<int32_t> > promise) {
    //blog(LOG_INFO, "@@@ do_encode");
    profile_start(do_encode_name);
    if (!encoder->profile_encoder_encode_name) {
        encoder->profile_encoder_encode_name = profile_store_name(obs_get_profiler_name_store(), "encode(%s)", encoder->context.name);
    }

    struct encoder_packet pkt = { 0 };
    bool received = false;
    bool success;

    pkt.timebase_num = encoder->timebase_num;
    pkt.timebase_den = encoder->timebase_den;
    pkt.encoder = encoder;

    if (send_key_frame_) {
        pkt.keyframe = true;
        send_key_frame_ = false;
    }

    profile_start(encoder->profile_encoder_encode_name);
    success = encoder->info.encode(encoder->context.data, frame, &pkt, &received);
    profile_end(encoder->profile_encoder_encode_name);
    if (!success) {
        last_encode_error = true;
        blog(LOG_ERROR, "Error encoding with encoder '%s'", encoder->context.name);
        goto error;
    }

    if (received) {
        if (!encoder->first_received) {
            encoder->offset_usec = packet_dts_usec(&pkt);
            encoder->first_received = true;
        }

        /* we use system time here to ensure sync with other encoders,
        * you do not want to use relative timestamps here */
        pkt.dts_usec = encoder->start_ts / 1000 + packet_dts_usec(&pkt) - encoder->offset_usec;
        pkt.sys_dts_usec = pkt.dts_usec;

        pthread_mutex_lock(&encoder->callbacks_mutex);

        for (size_t i = encoder->callbacks.num; i > 0; i--) {
            struct encoder_callback *cb;
            cb = encoder->callbacks.array + (i - 1);
            send_packet(encoder, cb, &pkt);
        }

        pthread_mutex_unlock(&encoder->callbacks_mutex);
    }

error:
    profile_end(do_encode_name);

    if (promise != NULL) {
        promise->set_value(0);
    }
}

void MediasoupVideoEncoder::send_packet(
    struct obs_encoder *encoder,
    struct encoder_callback *cb,
    struct encoder_packet *packet) {
    /* include SEI in first video packet */
    if (encoder->info.type == OBS_ENCODER_VIDEO && !cb->sent_first_packet) {
        send_first_video_packet(encoder, cb, packet);
    } else if (packet->keyframe) {
        //Add by HeZhen, WebRTC limit:Every key frame must begin with sps/pps.
        send_idr_packet(encoder, cb, packet);
    } else {
        cb->new_packet(cb->param, packet);
    }
}

void MediasoupVideoEncoder::send_first_video_packet(
    struct obs_encoder *encoder,
    struct encoder_callback *cb,
    struct encoder_packet *packet) {
    struct encoder_packet first_packet;
    DARRAY(uint8_t)       data;
    uint8_t               *sei;
    size_t                size;

    /* always wait for first keyframe */
    if (!packet->keyframe) {
        return;
    }

    da_init(data);

    //Add sps/pps first, modified by HeZhen.
    uint8_t *header;
    obs_encoder_get_extra_data(encoder, &header, &size);
    da_push_back_array(data, header, size);

    if (!get_sei(encoder, &sei, &size) || !sei || !size) {
        cb->new_packet(cb->param, packet);
        cb->sent_first_packet = true;
        return;
    }

    da_push_back_array(data, sei, size);
    da_push_back_array(data, packet->data, packet->size);

    first_packet = *packet;
    first_packet.data = data.array;
    first_packet.size = data.num;

    cb->new_packet(cb->param, &first_packet);
    cb->sent_first_packet = true;

    da_free(data);
}

void MediasoupVideoEncoder::send_idr_packet(
    struct obs_encoder *encoder,
    struct encoder_callback *cb,
    struct encoder_packet *packet) {
    DARRAY(uint8_t) data;
    da_init(data);
    uint8_t *header;
    size_t size;
    obs_encoder_get_extra_data(encoder, &header, &size);
    da_push_back_array(data, header, size);
    da_push_back_array(data, packet->data, packet->size);

    struct encoder_packet idr_packet;
    idr_packet = *packet;
    idr_packet.data = data.array;
    idr_packet.size = data.num;

    cb->new_packet(cb->param, &idr_packet);

    da_free(data);
}

bool MediasoupVideoEncoder::get_sei(
    const struct obs_encoder *encoder,
    uint8_t **sei,
    size_t *size) {
    if (encoder->info.get_sei_data) {
        return encoder->info.get_sei_data(encoder->context.data, sei, size);
    } else {
        return false;
    }
}

