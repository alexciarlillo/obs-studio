#ifndef _WEBRTCSTREAM_H_
#define _WEBRTCSTREAM_H_

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
#include "AudioDeviceModuleWrapper.h"
#include "obsWebrtcAudioSource.h"

// webrtc includes
#include "api/create_peerconnection_factory.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/set_remote_description_observer_interface.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/thread.h"
#include "rtc_base/timestamp_aligner.h"

// std lib
#include <initializer_list>
#include <regex>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

// class WebRTCStreamInterface :
//     public webrtc::PeerConnectionObserver,
//     public webrtc::CreateSessionDescriptionObserver,
//     public webrtc::SetSessionDescriptionObserver,
//     public webrtc::SetRemoteDescriptionObserverInterface {};

class WebRTCStream {
public:

    WebRTCStream(obs_output_t *output);
    ~WebRTCStream();

    bool close(bool wait);
    bool start();
    bool stop();
    void onAudioFrame(audio_data *frame);
    void onVideoFrame(video_data *frame);
    void setCodec(const std::string &new_codec) { this->video_codec = new_codec; }


    // Bitrate & dropped frames
    uint64_t getBitrate()        { return total_bytes_sent; }
    int getDroppedFrames()       { return pli_received; }

    template <typename T>
    rtc::scoped_refptr<T> make_scoped_refptr(T *t) {
        return rtc::scoped_refptr<T>(t);
    }

private:
    // Connection properties
    int audio_bitrate;
    int video_bitrate;
    std::string url;
    std::string username;
    std::string password;
    std::string audio_codec;
    std::string video_codec;
    int channel_count;

    uint16_t frame_id;
    uint64_t audio_bytes_sent;
    uint64_t video_bytes_sent;
    uint64_t total_bytes_sent;
    int pli_received;
    // Used to compute fps
    // NOTE ALEX: Should be initialized in constructor.
    std::chrono::system_clock::time_point previous_time
       = std::chrono::system_clock::time_point(std::chrono::duration<int>(0));
    uint32_t previous_frames_sent = 0;

    std::thread thread_closeAsync;

    rtc::CriticalSection crit_;

    // Audio Wrapper
    rtc::scoped_refptr<AudioDeviceModuleWrapper> adm;

    // Video Capturer
    rtc::scoped_refptr<VideoCapturer> videoCapturer;
    rtc::TimestampAligner timestamp_aligner_;

    // PeerConnection
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;

    // SetRemoteDescription observer
    rtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface> srd_observer;

    // Media stream
    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream;

    // Webrtc Source that wraps an OBS capturer
    rtc::scoped_refptr<obsWebrtcAudioSource> audio_source;

    // Tracks
    rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track;
    rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track;

    // WebRTC threads
    std::unique_ptr<rtc::Thread> network;
    std::unique_ptr<rtc::Thread> worker;
    std::unique_ptr<rtc::Thread> signaling;

    // OBS stream output
    obs_output_t *output;
};

#endif
