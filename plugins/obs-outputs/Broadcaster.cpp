#include "Broadcaster.h"
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <cpr/cpr.h>

#include <obs-module.h>

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

#define debug(format, ...) blog(LOG_DEBUG,   format, ##__VA_ARGS__)
#define info(format, ...)  blog(LOG_INFO,    format, ##__VA_ARGS__)
#define warn(format, ...)  blog(LOG_WARNING, format, ##__VA_ARGS__)
#define error(format, ...) blog(LOG_ERROR,   format, ##__VA_ARGS__)

Broadcaster::~Broadcaster()
{
  this->stop();
}

Broadcaster::Broadcaster(obs_output_t *output)
{
  frame_id = 0;

  // Store output
  this->output = output;
}

bool Broadcaster::start()
{
  // what do?

  // cricket::AudioOptions options;
  // options.echo_cancellation.emplace(false); // default: true
  // options.auto_gain_control.emplace(false); // default: true
  // options.noise_suppression.emplace(false); // default: true
  // options.highpass_filter.emplace(false);   // default: true
  // options.stereo_swapping.emplace(false);
  // options.typing_detection.emplace(false);  // default: true
  // options.experimental_agc.emplace(false);
  // // m79 options.extended_filter_aec.emplace(false);
  // // m79 options.delay_agnostic_aec.emplace(false);
  // options.experimental_ns.emplace(false);
  // options.residual_echo_detector.emplace(false); // default: true
  // // options.tx_agc_limiter.emplace(false);
  // audio_source = obsWebrtcAudioSource::Create(&options);

  // POST /test/rtctoken
  // get an RTC token for user cand channel
  info("Getting token");
  auto r = cpr::Post(cpr::Url{"http://localhost:8080/test/rtcToken"},
                    cpr::Body{"{\"userId\":\"xrdOWdyX\",\"channelId\":\"6057bdaf-c457-44c5-9e63-11d4614a86e1\"}"},
                    cpr::Header{{"guilded-test-run-id", "123"}, {"Content-Type", "application/json"}});
  info("Token response: %s", r.text.c_str());

  // /channels/:channelId/stream/room
  // get room router rtp parameters, attach fake token in header

  // /channels/:channelId/stream/connect
  // get send transport options


  if (r.status_code != 200)
  {
    info("Bad status code");
    return false;
  }
  else
  {
    info("Got OK");
    obs_output_begin_data_capture(output, 0);
    return true;
  }
}

bool Broadcaster::stop()
{
  obs_output_end_data_capture(output);
  return true;
}

void Broadcaster::onAudioFrame(audio_data *frame)
{
    if (!frame)
        return;
    // Push it to the device
    // audio_source->OnAudioData(frame);
}

void Broadcaster::onVideoFrame(video_data *frame)
{
    if (!frame)
        return;

    if (frame_id % 60 == 0) {
      info("Frame %d", frame_id);
    }

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

}