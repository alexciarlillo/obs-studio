// Copyright Dr. Alex. Gouaillard (2015, 2020)

#include "WebRTCStream.h"

#include "media-io/video-io.h"

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/video/i420_buffer.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "pc/rtc_stats_collector.h"
#include "rtc_base/checks.h"
#include "rtc_base/critical_section.h"
#include <libyuv.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <algorithm>
#include <locale>

#define debug(format, ...) blog(LOG_DEBUG,   format, ##__VA_ARGS__)
#define info(format, ...)  blog(LOG_INFO,    format, ##__VA_ARGS__)
#define warn(format, ...)  blog(LOG_WARNING, format, ##__VA_ARGS__)
#define error(format, ...) blog(LOG_ERROR,   format, ##__VA_ARGS__)

WebRTCStream::WebRTCStream(obs_output_t *output)
{
    frame_id = 0;
    pli_received = 0;
    audio_bytes_sent = 0;
    video_bytes_sent = 0;
    total_bytes_sent = 0;

    audio_bitrate = 128;
    video_bitrate = 2500;

    // Store output
    this->output = output;

    // Create audio device module
    // NOTE ALEX: check if we still need this
    adm = new rtc::RefCountedObject<AudioDeviceModuleWrapper>();

    // Network thread
    network = rtc::Thread::CreateWithSocketServer();
    network->SetName("network", nullptr);
    network->Start();

    // Worker thread
    worker = rtc::Thread::Create();
    worker->SetName("worker", nullptr);
    worker->Start();

    // Signaling thread
    signaling = rtc::Thread::Create();
    signaling->SetName("signaling", nullptr);
    signaling->Start();

    factory = webrtc::CreatePeerConnectionFactory(
            network.get(),
            worker.get(),
            signaling.get(),
            adm,
            webrtc::CreateBuiltinAudioEncoderFactory(),
            webrtc::CreateBuiltinAudioDecoderFactory(),
            webrtc::CreateBuiltinVideoEncoderFactory(),
            webrtc::CreateBuiltinVideoDecoderFactory(),
            nullptr,
            nullptr);

    // Create video capture module
    videoCapturer = new rtc::RefCountedObject<VideoCapturer>();
}

WebRTCStream::~WebRTCStream()
{
    // Shutdown websocket connection and close Peer Connection
    close(false);

    // Free factories
    adm = nullptr;
    pc = nullptr;
    factory = nullptr;
    videoCapturer = nullptr;

    // Stop all threads
    if (!network->IsCurrent())
        network->Stop();
    if (!worker->IsCurrent())
        worker->Stop();
    if (!signaling->IsCurrent())
        signaling->Stop();

    network.release();
    worker.release();
    signaling.release();
}

bool WebRTCStream::start()
{
    info("WebRTCStream::start");
    // Access service if started, or fail

    obs_service_t *service = obs_output_get_service(output);
    if (!service) {
        obs_output_set_last_error(
            output,
            "An unexpected error occurred during stream startup.");
        obs_output_signal_stop(output, OBS_OUTPUT_CONNECT_FAILED);
        return false;
    }

    // Extract setting from service

    url      = obs_service_get_url(service)      ? obs_service_get_url(service)      : "";
    username = obs_service_get_username(service) ? obs_service_get_username(service) : "";
    password = obs_service_get_password(service) ? obs_service_get_password(service) : "";
    // Some extra log
    info("Video codec: %s", video_codec.empty() ? "Automatic" : video_codec.c_str());

    // Stream setting sanity check

    bool isServiceValid = true;
    if (url.empty()) {
        warn("Invalid url");
        isServiceValid = false;
    }
	if (password.empty()) {
		warn("Missing Password");
		isServiceValid = false;
	}

    if (!isServiceValid) {
        obs_output_set_last_error(
            output,
            "Your service settings are not complete. Open the settings => stream window and complete them.");
        obs_output_signal_stop(output, OBS_OUTPUT_CONNECT_FAILED);
        return false;
    }

    // Set up encoders.
    // NOTE ALEX: should not be done for webrtc.

    obs_output_t *context = output;

    obs_encoder_t *aencoder = obs_output_get_audio_encoder(context, 0);
    obs_data_t *asettings = obs_encoder_get_settings(aencoder);
    audio_bitrate = (int)obs_data_get_int(asettings, "bitrate");
    obs_data_release(asettings);

    obs_encoder_t *vencoder = obs_output_get_video_encoder(context);
    obs_data_t *vsettings = obs_encoder_get_settings(vencoder);
    video_bitrate = (int)obs_data_get_int(vsettings, "bitrate");
    obs_data_release(vsettings);

    struct obs_audio_info audio_info;
    if (!obs_get_audio_info(&audio_info)) {
	warn("Failed to load audio settings.  Defaulting to opus.");
         audio_codec = "opus";
    } else {
         // NOTE ALEX: if input # channel > output we should down mix
         //            if input is > 2 but < 6 we might have a porblem with multiopus.
         channel_count = (int)(audio_info.speakers);
         audio_codec = channel_count <= 2 ? "opus" : "multiopus";
    }

    // Shutdown websocket connection and close Peer Connection (just in case)
    if (close(false))
        obs_output_signal_stop(output, OBS_OUTPUT_ERROR);


    // TODO (alexc): initialize mediasoup



    cricket::AudioOptions options;
    options.echo_cancellation.emplace(false); // default: true
    options.auto_gain_control.emplace(false); // default: true
    options.noise_suppression.emplace(false); // default: true
    options.highpass_filter.emplace(false);   // default: true
    options.stereo_swapping.emplace(false);
    options.typing_detection.emplace(false);  // default: true
    options.experimental_agc.emplace(false);
    // m79 options.extended_filter_aec.emplace(false);
    // m79 options.delay_agnostic_aec.emplace(false);
    options.experimental_ns.emplace(false);
    options.residual_echo_detector.emplace(false); // default: true
    // options.tx_agc_limiter.emplace(false);

    stream = factory->CreateLocalMediaStream("obs");

    audio_source = obsWebrtcAudioSource::Create(&options);
    audio_track = factory->CreateAudioTrack("audio", audio_source);
    // pc->AddTrack(audio_track, {"obs"});
    stream->AddTrack(audio_track);

    video_track = factory->CreateVideoTrack("video", videoCapturer);
    // pc->AddTrack(video_track, {"obs"});
    stream->AddTrack(video_track);

    // Add the stream to the peer connection


    // Extra logging

    info("Stream Key:       %s\n",
        password.c_str());

    info("CONNECTING TO %s", url.c_str());


    return true;
}





bool WebRTCStream::close(bool wait)
{
    // if (!pc.get())
    //     return false;
    // // Get pointer
    // auto old = pc.release();
    // // Close Peer Connection
    // old->Close();
    // // Shutdown websocket connection
    // // if (client) {
    // //     client->disconnect(wait);
    // //     delete(client);
    // //     client = nullptr;
    // // }
    return true;
}

bool WebRTCStream::stop()
{
    info("WebRTCStream::stop");
    // Shutdown websocket connection and close Peer Connection
    close(true);
    // Disconnect, this will call stop on main thread
    obs_output_end_data_capture(output);
    return true;
}

void WebRTCStream::onAudioFrame(audio_data *frame)
{
    if (!frame)
        return;
    // Push it to the device
    audio_source->OnAudioData(frame);
}

void WebRTCStream::onVideoFrame(video_data *frame)
{
    if (!frame)
        return;
    if (!videoCapturer)
        return;

    if (std::chrono::system_clock::time_point(std::chrono::duration<int>(0)) == previous_time)
        // First frame sent: Initialize previous_time
        previous_time = std::chrono::system_clock::now();

    // Calculate size
    int outputWidth = obs_output_get_width(output);
    int outputHeight = obs_output_get_height(output);
    auto videoType = webrtc::VideoType::kNV12;
    uint32_t size = outputWidth * outputHeight * 3 / 2;

    int stride_y = outputWidth;
    int stride_uv = (outputWidth + 1) / 2;
    int target_width = abs(outputWidth);
    int target_height = abs(outputHeight);

    // Convert frame
    rtc::scoped_refptr<webrtc::I420Buffer> buffer = webrtc::I420Buffer::Create(
            target_width, target_height, stride_y, stride_uv, stride_uv);

    libyuv::RotationMode rotation_mode = libyuv::kRotate0;

    const int conversionResult = libyuv::ConvertToI420(
            frame->data[0], size,
            buffer.get()->MutableDataY(), buffer.get()->StrideY(),
            buffer.get()->MutableDataU(), buffer.get()->StrideU(),
            buffer.get()->MutableDataV(), buffer.get()->StrideV(), 0, 0,
            outputWidth, outputHeight, target_width, target_height,
            rotation_mode, ConvertVideoType(videoType));

    // not using the result yet, silence compiler
    (void)conversionResult;

    const int64_t obs_timestamp_us =
            (int64_t)frame->timestamp / rtc::kNumNanosecsPerMicrosec;

    // Align timestamps from OBS capturer with rtc::TimeMicros timebase
    const int64_t aligned_timestamp_us =
            timestamp_aligner_.TranslateTimestamp(obs_timestamp_us, rtc::TimeMicros());

    // Create a webrtc::VideoFrame to pass to the capturer
    webrtc::VideoFrame video_frame =
            webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_rotation(webrtc::kVideoRotation_0)
            .set_timestamp_us(aligned_timestamp_us)
            .set_id(++frame_id)
            .build();

    // Send frame to video capturer
    videoCapturer->OnFrameCaptured(video_frame);
}
