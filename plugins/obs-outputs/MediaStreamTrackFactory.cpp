#define MSC_CLASS "MediaStreamTrackFactory"

#include <iostream>

#include "MediaSoupClientErrors.hpp"
#include "MediaStreamTrackFactory.h"
#include "pc/test/fake_audio_capture_module.h"
#include "pc/test/fake_periodic_video_track_source.h"
#include "pc/test/frame_generator_capturer_video_track_source.h"
#include "system_wrappers/include/clock.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "media/base/adapted_video_track_source.h"
#include "api/media_stream_interface.h"
#include "AudioDeviceModuleWrapper.h"

using namespace mediasoupclient;

static rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;

/* MediaStreamTrack holds reference to the threads of the PeerConnectionFactory.
 * Use plain pointers in order to avoid threads being destructed before tracks.
 */
static rtc::Thread* networkThread;
static rtc::Thread* signalingThread;
static rtc::Thread* workerThread;
// Audio Wrapper
static rtc::scoped_refptr<AudioDeviceModuleWrapper> adm;

static void createFactory()
{
  networkThread   = rtc::Thread::Create().release();
  signalingThread = rtc::Thread::Create().release();
  workerThread    = rtc::Thread::Create().release();

  networkThread->SetName("network_thread", nullptr);
  signalingThread->SetName("signaling_thread", nullptr);
  workerThread->SetName("worker_thread", nullptr);

  if (!networkThread->Start() || !signalingThread->Start() || !workerThread->Start())
  {
    MSC_THROW_INVALID_STATE_ERROR("thread start errored");
  }

  // Create audio device module
  // NOTE ALEX: check if we still need this
  adm = new rtc::RefCountedObject<AudioDeviceModuleWrapper>();

  webrtc::PeerConnectionInterface::RTCConfiguration config;

  factory = webrtc::CreatePeerConnectionFactory(
    networkThread,
    workerThread,
    signalingThread,
    adm,
    webrtc::CreateBuiltinAudioEncoderFactory(),
    webrtc::CreateBuiltinAudioDecoderFactory(),
    webrtc::CreateBuiltinVideoEncoderFactory(),
    webrtc::CreateBuiltinVideoDecoderFactory(),
    nullptr /*audio_mixer*/,
    nullptr /*audio_processing*/);

  if (!factory)
  {
    MSC_THROW_ERROR("error ocurred creating peerconnection factory");
  }
}

// Audio track creation.
rtc::scoped_refptr<webrtc::AudioTrackInterface> createAudioTrack(const std::string& label, webrtc::AudioSourceInterface *source)
{
  if (!factory)
    createFactory();

  return factory->CreateAudioTrack(label, source);
}

// Video track creation.
rtc::scoped_refptr<webrtc::VideoTrackInterface> createVideoTrack(const std::string& label, rtc::AdaptedVideoTrackSource *source)
{
  if (!factory)
    createFactory();

  return factory->CreateVideoTrack(label, source);
}
