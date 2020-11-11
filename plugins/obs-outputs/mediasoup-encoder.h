#ifndef __BEE_OBS_VIDEO_ENCODER_H__
#define __BEE_OBS_VIDEO_ENCODER_H__

#include <obs-module.h>

#include "api/video_codecs/video_encoder_factory.h"
#include "api/units/data_rate.h"
#include "api/video_codecs/sdp_video_format.h"

#include "webrtc/api/video_codecs/video_encoder.h"
#include "webrtc/common_video/h264/h264_bitstream_parser.h"
#include "webrtc/system_wrappers/include/clock.h"

#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "media/engine/simulcast_encoder_adapter.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/time_utils.h"

#include <future>
#include <mutex>

///////////////////////////////////MediasoupVideoEncoder///////////////////////////////////////
class IOService;
class MediasoupVideoEncoder : public webrtc::VideoEncoder {
public:
    MediasoupVideoEncoder();
    virtual ~MediasoupVideoEncoder();

public:
    bool open(const ConfigFile& config);
    bool close();
    bool start();
    bool stop();
    void obs_encode(struct video_data *frame);

    //WebRTC interfaces.
    virtual int32_t InitEncode(
        const webrtc::VideoCodec* codec_settings,
        int32_t number_of_cores,
        size_t max_payload_size) override;

    virtual int32_t RegisterEncodeCompleteCallback(
        webrtc::EncodedImageCallback* callback) override;

    virtual int32_t Release() override;

    virtual int32_t Encode(
        const webrtc::VideoFrame& frame,
        const webrtc::CodecSpecificInfo* codec_specific_info,
        const std::vector<webrtc::FrameType>* frame_types) override;

    virtual int32_t SetChannelParameters(
        uint32_t packet_loss,
        int64_t rtt) override;

private:
    bool start_internal(obs_encoder_t *encoder);
    bool stop_internal(
        obs_encoder_t *encoder,
        void(*new_packet)(void *param, struct encoder_packet *packet),
        void *param);
    void configure_encoder(const ConfigFile& config);
    static void new_encoded_packet(void *param, struct encoder_packet *packet);
    void on_new_encoded_packet(struct encoder_packet *packet);
    void load_streaming_preset_h264(const char *encoderId);
    void update_streaming_settings_amd(obs_data_t *settings, int bitrate);
    size_t get_callback_idx(
        const struct obs_encoder *encoder,
        void(*new_packet)(void *param, struct encoder_packet *packet),
        void *param);
    void obs_encoder_actually_destroy(obs_encoder_t *encoder);
    void free_audio_buffers(struct obs_encoder *encoder);

    //WebRTC interface implement.
    void InitEncodeOnCodecThread(
        int32_t width,
        int32_t height,
        int32_t target_bitrate,
        int32_t fps,
        std::shared_ptr<std::promise<int32_t> > promise);

    void RegisterEncodeCompleteCallbackOnCodecThread(
        webrtc::EncodedImageCallback* callback,
        std::shared_ptr<std::promise<int32_t> > promise);

    void ReleaseOnCodecThread(
        std::shared_ptr<std::promise<int32_t> > promise);

    void EncodeOnCodecThread(
        const webrtc::VideoFrame& frame,
        const webrtc::FrameType frame_type,
        const int64_t frame_input_time_ms);

    //OBS encoder implement.

    void do_encode(
        struct obs_encoder *encoder,
        struct encoder_frame *frame,
        std::shared_ptr<std::promise<int32_t> > promise);

    void send_packet(
        struct obs_encoder *encoder,
        struct encoder_callback *cb, struct encoder_packet *packet);

    void send_first_video_packet(
        struct obs_encoder *encoder,
        struct encoder_callback *cb,
        struct encoder_packet *packet);

    void send_idr_packet(
        struct obs_encoder *encoder,
        struct encoder_callback *cb,
        struct encoder_packet *packet);

    bool get_sei(
        const struct obs_encoder *encoder,
        uint8_t **sei,
        size_t *size);

private:
    OBSEncoder h264_encoder_;
    std::shared_ptr<IOService> io_service_;
    webrtc::EncodedImageCallback* callback_ = NULL;
    bool opened = false;
    bool started = false;
    bool last_encode_error = false;
    int32_t width_ = 0;
    int32_t height_ = 0;
    int32_t target_bitrate_ = 0;
    int32_t fps_ = 0;
    webrtc::H264BitstreamParser h264_bitstream_parser_;

    struct InputFrameInfo {
        InputFrameInfo(
            int64_t encode_start_time,
            int32_t frame_timestamp,
            int64_t frame_render_time_ms,
            webrtc::VideoRotation rotation)
            : encode_start_time(encode_start_time),
            frame_timestamp(frame_timestamp),
            frame_render_time_ms(frame_render_time_ms),
            rotation(rotation) {}
        // Time when video frame is sent to encoder input.
        const int64_t encode_start_time;
        // Input frame information.
        const int32_t frame_timestamp;
        const int64_t frame_render_time_ms;
        const webrtc::VideoRotation rotation;
    };

    std::list<InputFrameInfo> input_frame_infos_;
    int32_t output_timestamp_;               // Last output frame timestamp from |input_frame_infos_|.
    int64_t output_render_time_ms_;          // Last output frame render time from |input_frame_infos_|.
    webrtc::VideoRotation output_rotation_;  // Last output frame rotation from |input_frame_infos_|.

    webrtc::Clock* const clock_;
    const int64_t delta_ntp_internal_ms_;
    bool send_key_frame_ = false;
};


#endif // #ifndef __BEE_OBS_VIDEO_ENCODER_H__
