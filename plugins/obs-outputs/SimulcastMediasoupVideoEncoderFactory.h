#ifndef SIMULCAST_MEDIASOUP_VIDEO_ENCODER_FACTORY_H_
#define SIMULCAST_MEDIASOUP_VIDEO_ENCODER_FACTORY_H_


#include "api/video_codecs/video_encoder_factory.h"

std::unique_ptr<webrtc::VideoEncoderFactory> CreateSimulcastMediasoupVideoEncoderFactory();

#endif // SIMULCAST_MEDIASOUP_VIDEO_ENCODER_FACTORY_H_