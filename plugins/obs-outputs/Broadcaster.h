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

#include "rtc_base/timestamp_aligner.h"

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <regex>
#include <vector>
#include <thread>

class Broadcaster
{
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

  uint16_t frame_id;

  // Video Capturer
  rtc::TimestampAligner timestamp_aligner_;

  // Webrtc Source that wraps an OBS capturer
  // rtc::scoped_refptr<obsWebrtcAudioSource> audio_source;

  // OBS stream output
  obs_output_t *output;
};

#endif // STOKER_HPP