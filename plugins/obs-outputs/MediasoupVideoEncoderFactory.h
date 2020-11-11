#ifndef MEDIASOUP_VIDEO_ENCODER_FACTORY_H_
#define MEDIASOUP_VIDEO_ENCODER_FACTORY_H_

#include <memory>
#include <vector>

#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"

class MediasoupVideoEncoderFactory : public webrtc::VideoEncoderFactory {
    public:
        MediasoupVideoEncoderFactory();

        std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;
        webrtc::VideoEncoderFactory::CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const override;
        std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& format) override;

    private:
        std::vector<webrtc::SdpVideoFormat> supported_formats;
};

#endif // MEDIASOUP_VIDEO_ENCODER_FACTORY_H_