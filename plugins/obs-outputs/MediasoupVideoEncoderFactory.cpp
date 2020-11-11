#include "MediasoupVideoEncoderFactory.h"
// #include "mediasoup-encoder.h"

#include "common_types.h"
#include "api/video_codecs/sdp_video_format.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "media/base/h264_profile_level_id.h"
#include "modules/video_coding/codecs/h264/include/h264.h"

MediasoupVideoEncoderFactory::MediasoupVideoEncoderFactory() {
    // The internal encoder of WebRTC will send a 42e01f encoding capability in sdp, which is kProfileConstrainedBaseline+3.1.
    // mediasoup will only negotiate one, it must be set to 42e01f, otherwise the external encoder cannot be enabled.


    // cricket::VideoCodec codec(cricket::kH264CodecName);
    // const webrtc::H264::ProfileLevelId profile(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel3_1);
    // std::string s = *webrtc::H264::ProfileLevelIdToString(profile);
    // codec.SetParam(cricket::kH264FmtpProfileLevelId, *webrtc::H264::ProfileLevelIdToString(profile));
    // codec.SetParam(cricket::kH264FmtpLevelAsymmetryAllowed, "1");
    // codec.SetParam(cricket::kH264FmtpPacketizationMode, "1");

    supported_formats.push_back(webrtc::CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel3_1, "1"));
}

std::vector<webrtc::SdpVideoFormat> MediasoupVideoEncoderFactory::GetSupportedFormats() const {
    return supported_formats;
}

webrtc::VideoEncoderFactory::CodecInfo MediasoupVideoEncoderFactory::QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const {
    webrtc::VideoEncoderFactory::CodecInfo info;
    info.has_internal_source = false;
    info.is_hardware_accelerated = false; // maybe??
    return info;
};

std::unique_ptr<webrtc::VideoEncoder> MediasoupVideoEncoderFactory::CreateVideoEncoder(const webrtc::SdpVideoFormat& format) {
    // TODO
    // return MediasoupVideoEncoder::Create();
    return nullptr;
};
