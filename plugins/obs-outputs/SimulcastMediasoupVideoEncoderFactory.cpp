#include "SimulcastMediasoupVideoEncoderFactory.h"
#include "MediasoupVideoEncoderFactory.h"

#include <memory>
#include <vector>

#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "media/engine/encoder_simulcast_proxy.h"
#include "rtc_base/checks.h"

bool IsFormatSupported(const std::vector<webrtc::SdpVideoFormat>& supported_formats,
                       const webrtc::SdpVideoFormat& format) {
  for (const webrtc::SdpVideoFormat& supported_format : supported_formats) {
    if (cricket::IsSameCodec(format.name, format.parameters,
                             supported_format.name,
                             supported_format.parameters)) {
      return true;
    }
  }
  return false;
}


class SimulcastMediasoupVideoEncoderFactory : public webrtc::VideoEncoderFactory {
    public:
        SimulcastMediasoupVideoEncoderFactory()
            : internal_encoder_factory(new MediasoupVideoEncoderFactory()) {}

        webrtc::VideoEncoderFactory::CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const override {
            // Format must be one of the internal formats.
            RTC_DCHECK(IsFormatSupported(internal_encoder_factory->GetSupportedFormats(), format));
            webrtc::VideoEncoderFactory::CodecInfo info;
            info.has_internal_source = false;
            info.is_hardware_accelerated = false;
            return info;
        }

        std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& format) override {
            // Try creating internal encoder.
            std::unique_ptr<webrtc::VideoEncoder> internal_encoder;
            if (IsFormatSupported(internal_encoder_factory->GetSupportedFormats(), format)) {
              internal_encoder = std::make_unique<webrtc::EncoderSimulcastProxy>(
                  internal_encoder_factory.get(), format);
            }

            return internal_encoder;
        }

        std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
            return internal_encoder_factory->GetSupportedFormats();
        }

    private:
        const std::unique_ptr<webrtc::VideoEncoderFactory> internal_encoder_factory;
};
