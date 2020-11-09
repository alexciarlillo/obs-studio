#include "Broadcaster.h"
#include "MediaStreamTrackFactory.h"
#include "mediasoupclient.hpp"
#include "json.hpp"
#include <chrono>
#include <cpr/cpr.h>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

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

using json = nlohmann::json;

#define debug(format, ...) blog(LOG_DEBUG,   format, ##__VA_ARGS__)
#define info(format, ...)  blog(LOG_INFO,    format, ##__VA_ARGS__)
#define warn(format, ...)  blog(LOG_WARNING, format, ##__VA_ARGS__)
#define error(format, ...) blog(LOG_ERROR,   format, ##__VA_ARGS__)

Broadcaster::~Broadcaster()
{
  this->stop();
}

Broadcaster::Broadcaster(
  obs_output_t *output,
  bool enableAudio,
  bool useSimulcast,
  bool verifySsl)
{
  frame_id = 0;

  // Store output
  this->output = output;

  // other params
  this->enableAudio = enableAudio;
  this->useSimulcast = useSimulcast;
  this->verifySsl = verifySsl;

  // Create video capture module
  videoCapturer = new rtc::RefCountedObject<VideoCapturer>();
}



/* Transport::Listener::OnConnect
 *
 * Fired for the first Transport::Consume() or Transport::Produce().
 * Update the already created remote transport with the local DTLS parameters.
 */
std::future<void> Broadcaster::OnConnect(mediasoupclient::Transport* transport, const json& dtlsParameters)
{
  info("[INFO] Broadcaster::OnConnect()");
  // info("[INFO] dtlsParameters: " << dtlsParameters.dump(4));

  if (transport->GetId() == this->sendTransport->GetId())
  {
    return this->OnConnectSendTransport(dtlsParameters);
  }

  else
  {
    std::promise<void> promise;

    promise.set_exception(std::make_exception_ptr("Unknown transport requested to connect"));

    return promise.get_future();
  }
}

std::future<void> Broadcaster::OnConnectSendTransport(const json& dtlsParameters)
{
  std::promise<void> promise;

  /* clang-format off */
  json body =
  {
    { "dtlsParameters", dtlsParameters }
  };
  /* clang-format on */

  auto r = cpr::PostAsync(
             cpr::Url{ this->url + "/broadcasters/" + this->id + "/transports/" +
                       this->sendTransport->GetId() + "/connect" },
             cpr::Body{ body.dump() },
             cpr::Header{ { "Content-Type", "application/json" } },
             cpr::VerifySsl{ verifySsl })
             .get();

  if (r.status_code == 200)
  {
    promise.set_value();
  }
  else
  {
    std::cerr << "[ERROR] unable to connect transport"
              << " [status code:" << r.status_code << ", body:\"" << r.text << "\"]" << std::endl;

    promise.set_exception(std::make_exception_ptr(r.text));
  }

  return promise.get_future();
}

/*
 * Transport::Listener::OnConnectionStateChange.
 */
void Broadcaster::OnConnectionStateChange(
  mediasoupclient::Transport* /*transport*/, const std::string& connectionState)
{
  std::cout << "[INFO] Broadcaster::OnConnectionStateChange() [connectionState:" << connectionState
            << "]" << std::endl;

  if (connectionState == "failed")
  {
    stop();
    std::exit(0);
  }
}

/* Producer::Listener::OnProduce
 *
 * Fired when a producer needs to be created in mediasoup.
 * Retrieve the remote producer ID and feed the caller with it.
 */
std::future<std::string> Broadcaster::OnProduce(
  mediasoupclient::SendTransport* /*transport*/,
  const std::string& kind,
  json rtpParameters,
  const json& /*appData*/)
{
  info("[INFO] Broadcaster::OnProduce()");
  // info("[INFO] rtpParameters: " << rtpParameters.dump(4));

  std::promise<std::string> promise;

  /* clang-format off */
  json body =
  {
    { "kind",          kind          },
    { "rtpParameters", rtpParameters }
  };
  /* clang-format on */

  auto r = cpr::PostAsync(
             cpr::Url{ this->url + "/broadcasters/" + this->id + "/transports/" +
                       this->sendTransport->GetId() + "/producers" },
             cpr::Body{ body.dump() },
             cpr::Header{ { "Content-Type", "application/json" } },
             cpr::VerifySsl{ verifySsl })
             .get();

  if (r.status_code == 200)
  {
    auto response = json::parse(r.text);

    auto it = response.find("id");
    if (it == response.end() || !it->is_string())
    {
      promise.set_exception(std::make_exception_ptr("'id' missing in response"));
    }

    promise.set_value((*it).get<std::string>());
  }
  else
  {
    std::cerr << "[ERROR] unable to create producer"
              << " [status code:" << r.status_code << ", body:\"" << r.text << "\"]" << std::endl;

    promise.set_exception(std::make_exception_ptr(r.text));
  }

  return promise.get_future();
}

bool Broadcaster::start()
{
  info("[INFO] Broadcaster::Start()");

  // Service Setup

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


  info("[INFO] verifying that room exists and getting rtpParameters");
  auto r = cpr::GetAsync(cpr::Url{ url }, cpr::VerifySsl{ verifySsl }).get();

  if (r.status_code != 200)
  {
    info("[ERROR] unable to retrieve room info  [status code: %s, body:\"%s\"]");

  }
  else
  {
    info("[INFO] got room");
  }

  auto response = nlohmann::json::parse(r.text);

  info("[INFO] loading device");
  // Load the device.
  device.Load(response);

  /* clang-format off */
  json body =
  {
    { "id",          this->id          },
    { "displayName", "broadcaster"     },
    { "device",
      {
        { "name",    "libmediasoupclient"       },
        { "version", mediasoupclient::Version() }
      }
    },
    { "rtpCapabilities", this->device.GetRtpCapabilities() }
  };
  /* clang-format on */

  r = cpr::PostAsync(
             cpr::Url{ this->url + "/broadcasters" },
             cpr::Body{ body.dump() },
             cpr::Header{ { "Content-Type", "application/json" } },
             cpr::VerifySsl{ verifySsl })
             .get();

  if (r.status_code != 200)
  {
    std::cerr << "[ERROR] unable to create Broadcaster"
              << " [status code:" << r.status_code << ", body:\"" << r.text << "\"]" << std::endl;

    return false;
  }

  this->CreateSendTransport(enableAudio, useSimulcast);

  return true;
}


void Broadcaster::CreateSendTransport(bool enableAudio, bool useSimulcast)
{
  info("[INFO] creating mediasoup send WebRtcTransport...");

  json sctpCapabilities = this->device.GetSctpCapabilities();
  /* clang-format off */
  json body =
  {
    { "type",    "webrtc" },
    { "rtcpMux", true     },
    { "sctpCapabilities", sctpCapabilities }
  };
  /* clang-format on */

  auto r = cpr::PostAsync(
             cpr::Url{ this->url + "/broadcasters/" + this->id + "/transports" },
             cpr::Body{ body.dump() },
             cpr::Header{ { "Content-Type", "application/json" } },
             cpr::VerifySsl{ verifySsl })
             .get();

  if (r.status_code != 200)
  {
    std::cerr << "[ERROR] unable to create send mediasoup WebRtcTransport"
              << " [status code:" << r.status_code << ", body:\"" << r.text << "\"]" << std::endl;

    return;
  }

  auto response = json::parse(r.text);

  if (response.find("id") == response.end())
  {
    info("[ERROR] 'id' missing in response");

    return;
  }
  else if (response.find("iceParameters") == response.end())
  {
    info("[ERROR] 'iceParametersd' missing in response");

    return;
  }
  else if (response.find("iceCandidates") == response.end())
  {
    info("[ERROR] 'iceCandidates' missing in response");

    return;
  }
  else if (response.find("dtlsParameters") == response.end())
  {
    info("[ERROR] 'dtlsParameters' missing in response");

    return;
  }
  else if (response.find("sctpParameters") == response.end())
  {
    info("[ERROR] 'sctpParameters' missing in response");

    return;
  }

  info("[INFO] creating SendTransport...");

  auto sendTransportId = response["id"].get<std::string>();

  this->sendTransport = this->device.CreateSendTransport(
    this,
    sendTransportId,
    response["iceParameters"],
    response["iceCandidates"],
    response["dtlsParameters"],
    response["sctpParameters"]);

  ///////////////////////// Create Audio Producer //////////////////////////

  if (enableAudio && this->device.CanProduce("audio"))
  {
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


    audio_source = obsWebrtcAudioSource::Create(&options);
    audio_track = createAudioTrack(std::to_string(rtc::CreateRandomId()), audio_source);

    /* clang-format off */
    json codecOptions = {
      { "opusStereo", true },
      { "opusDtx",    true }
    };
    /* clang-format on */

    this->sendTransport->Produce(this, audio_track, nullptr, &codecOptions);
  }
  else
  {
    info("[WARN] cannot produce audio");
  }

  ///////////////////////// Create Video Producer //////////////////////////

  if (this->device.CanProduce("video"))
  {
    auto videoTrack = createVideoTrack(std::to_string(rtc::CreateRandomId()), videoCapturer);

    if (useSimulcast)
    {
      std::vector<webrtc::RtpEncodingParameters> encodings;
      encodings.emplace_back(webrtc::RtpEncodingParameters());
      encodings.emplace_back(webrtc::RtpEncodingParameters());
      encodings.emplace_back(webrtc::RtpEncodingParameters());

      this->sendTransport->Produce(this, videoTrack, &encodings, nullptr);
    }
    else
    {
      this->sendTransport->Produce(this, videoTrack, nullptr, nullptr);
    }
  }
  else
  {
    info("[WARN] cannot produce video");

    return;
  }
}

bool Broadcaster::stop()
{
  info("[INFO] Broadcaster::stop()");

  if (this->recvTransport)
  {
    recvTransport->Close();
  }

  if (this->sendTransport)
  {
    sendTransport->Close();
  }

  cpr::DeleteAsync(
    cpr::Url{ this->url + "/broadcasters/" + this->id }, cpr::VerifySsl{ verifySsl })
    .get();

  return true;
}

void Broadcaster::OnTransportClose(mediasoupclient::Producer* /*producer*/)
{
  info("[INFO] Broadcaster::OnTransportClose()");
}

/* Producer::Listener::OnProduceData
 *
 * Fired when a data producer needs to be created in mediasoup.
 * Retrieve the remote producer ID and feed the caller with it.
 */
std::future<std::string> Broadcaster::OnProduceData(
  mediasoupclient::SendTransport* /*transport*/,
  const json& sctpStreamParameters,
  const std::string& label,
  const std::string& protocol,
  const json& /*appData*/)
{
  std::cout << "[INFO] Broadcaster::OnProduceData()" << std::endl;
  // std::cout << "[INFO] rtpParameters: " << rtpParameters.dump(4) << std::endl;

  std::promise<std::string> promise;
  std::string id = "not-implemented";
  promise.set_value(id);
  return promise.get_future();
}

void Broadcaster::onAudioFrame(audio_data *frame)
{
    if (!frame)
        return;
    // Push it to the device
    audio_source->OnAudioData(frame);
}

void Broadcaster::onVideoFrame(video_data *frame)
{
    if (!frame)
        return;
    if (!videoCapturer)
        return;

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