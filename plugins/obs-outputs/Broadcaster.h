#ifndef BROADCASTER_H
#define BROADCASTER_H

#if WIN32
#pragma comment(lib,"Strmiids.lib")
#pragma comment(lib,"Secur32.lib")
#pragma comment(lib,"Msdmo.lib")
#pragma comment(lib,"dmoguids.lib")
#pragma comment(lib,"wmcodecdspuuid.lib")
#pragma comment(lib,"amstrmid.lib")
#endif

// lib obs includes
#include "obs.h"

#include "VideoCapturer.h"
#include "obsWebrtcAudioSource.h"
#include "rtc_base/timestamp_aligner.h"
#include "mediasoupclient.hpp"
#include "json.hpp"

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <regex>
#include <vector>
#include <thread>

class BroadcasterInterface : public mediasoupclient::SendTransport::Listener,
                   public mediasoupclient::Producer::Listener {};

class Broadcaster : public rtc::RefCountedObject<BroadcasterInterface>
{

  /* Virtual methods inherited from SendTransport::Listener. */
public:
  std::future<void> OnConnect(
    mediasoupclient::Transport* transport, const nlohmann::json& dtlsParameters) override;
  void OnConnectionStateChange(
    mediasoupclient::Transport* transport, const std::string& connectionState) override;
  std::future<std::string> OnProduce(
    mediasoupclient::SendTransport* /*transport*/,
    const std::string& kind,
    nlohmann::json rtpParameters,
    const nlohmann::json& appData) override;
  std::future<std::string> OnProduceData(
    mediasoupclient::SendTransport* transport,
    const nlohmann::json& sctpStreamParameters,
    const std::string& label,
    const std::string& protocol,
    const nlohmann::json& appData) override;

/* Virtual methods inherited from Producer::Listener. */
public:
  void OnTransportClose(mediasoupclient::Producer* producer) override;

public:
  Broadcaster(obs_output_t *output);
  ~Broadcaster();

  bool start();
  bool stop();
  void onAudioFrame(audio_data *frame);
  void onVideoFrame(video_data *frame);
  void setCodec(const std::string &new_codec) { this->video_codec = new_codec; }

private:
  std::string url;
  std::string key;
  std::string username;
  std::string password;
  std::string audio_codec;
  std::string video_codec;
  std::string token;

  std::string id = std::to_string(rtc::CreateRandomId());
  std::string channelId = "6057bdaf-c457-44c5-9e63-11d4614a86e1";
  std::string userId = "xrdOWdyX";
  std::string apiUrl = "http://localhost:8080";
  std::string rtcUrl = "http://localhost:8007";

  mediasoupclient::Device device;
  mediasoupclient::SendTransport* sendTransport{ nullptr };
  mediasoupclient::RecvTransport* recvTransport{ nullptr };
  mediasoupclient::DataProducer* dataProducer{ nullptr };
  mediasoupclient::DataConsumer* dataConsumer{ nullptr };

  std::future<void> OnConnectSendTransport(const nlohmann::json& dtlsParameters);

  void Connect();

  uint16_t frame_id;

  // Video Capturer
  rtc::scoped_refptr<VideoCapturer> videoCapturer;
  rtc::TimestampAligner timestamp_aligner_;

  // Webrtc Source that wraps an OBS capturer
  rtc::scoped_refptr<obsWebrtcAudioSource> audio_source;

  // Tracks
  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track;
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track;

  // OBS stream output
  obs_output_t *output;
};

#endif // STOKER_HPP