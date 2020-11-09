#include <stdio.h>
#include <obs-module.h>
#include <obs-avc.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <util/threading.h>
#include <inttypes.h>
#include <modules/audio_processing/include/audio_processing.h>
#include "mediasoupclient.hpp"
#include <cpr/cpr.h>
#include "Broadcaster.h"

#define warn(format, ...)  blog(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  blog(LOG_INFO,    format, ##__VA_ARGS__)
#define debug(format, ...) blog(LOG_DEBUG,   format, ##__VA_ARGS__)

#define OPT_DROP_THRESHOLD "drop_threshold_ms"
#define OPT_PFRAME_DROP_THRESHOLD "pframe_drop_threshold_ms"
#define OPT_MAX_SHUTDOWN_TIME_SEC "max_shutdown_time_sec"
#define OPT_BIND_IP "bind_ip"
#define OPT_NEWSOCKETLOOP_ENABLED "new_socket_loop_enabled"
#define OPT_LOWLATENCY_ENABLED "low_latency_mode_enabled"


extern "C" const char *mediasoup_stream_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("MEDIASOUPStream");
}

extern "C" void mediasoup_stream_destroy(void *data)
{
	info("mediasoup_stream_destroy");
}

extern "C" void *mediasoup_stream_create(obs_data_t *settings, obs_output_t *output)
{
	UNUSED_PARAMETER(settings);
	info("mediasoup_stream_create");

	mediasoupclient::Initialize();

	Broadcaster *broadcaster = new Broadcaster(output, true, true, false);
	return (void*)broadcaster;
}

extern "C" void mediasoup_stream_stop(void *data, uint64_t ts)
{
	info("mediasoup_stream_stop");
	UNUSED_PARAMETER(ts);

	Broadcaster *broadcaster = (Broadcaster*)data;
	broadcaster->stop();
	//Remove ref and let it self destroy
	broadcaster->Release();
}

extern "C" bool mediasoup_stream_start(void *data)
{
	info("mediasoup_stream_start");
	//Get stream
	Broadcaster *broadcaster = (Broadcaster*)data;

	return broadcaster->start();
}

extern "C" void mediasoup_receive_video(void *data, struct video_data *frame)
{
	Broadcaster *broadcaster = (Broadcaster*)data;
	broadcaster->onVideoFrame(frame);
}

extern "C" void mediasoup_receive_audio(void *data, struct audio_data *frame)
{
	Broadcaster *broadcaster = (Broadcaster*)data;
	broadcaster->onAudioFrame(frame);
}

extern "C" void mediasoup_receive_multitrack_audio(void *data, size_t idx, struct audio_data *frame)
{
	Broadcaster *broadcaster = (Broadcaster*)data;
	broadcaster->onAudioFrame(frame);
}

extern "C" void mediasoup_stream_defaults(obs_data_t *defaults)
{
	obs_data_set_default_int(defaults, OPT_DROP_THRESHOLD, 700);
	obs_data_set_default_int(defaults, OPT_PFRAME_DROP_THRESHOLD, 900);
	obs_data_set_default_int(defaults, OPT_MAX_SHUTDOWN_TIME_SEC, 30);
	obs_data_set_default_string(defaults, OPT_BIND_IP, "default");
	obs_data_set_default_bool(defaults, OPT_NEWSOCKETLOOP_ENABLED, false);
	obs_data_set_default_bool(defaults, OPT_LOWLATENCY_ENABLED, false);
}

extern "C" obs_properties_t *mediasoup_stream_properties(void *unused)
{
	info("mediasoup_stream_properties");
	UNUSED_PARAMETER(unused);

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_int(props, OPT_DROP_THRESHOLD,
			obs_module_text("MEDIASOUPStream.DropThreshold"),
			200, 10000, 100);

	obs_properties_add_bool(props, OPT_NEWSOCKETLOOP_ENABLED,
			obs_module_text("MEDIASOUPStream.NewSocketLoop"));
	obs_properties_add_bool(props, OPT_LOWLATENCY_ENABLED,
			obs_module_text("MEDIASOUPStream.LowLatencyMode"));

	return props;
}


extern "C" {
#ifdef _WIN32
	struct obs_output_info mediasoup_output_info = {
		"mediasoup_output", //id
		OBS_OUTPUT_AV | OBS_OUTPUT_SERVICE, //flags
		mediasoup_stream_getname, //get_name
		mediasoup_stream_create, //create
		mediasoup_stream_destroy, //destroy
		mediasoup_stream_start, //start
		mediasoup_stream_stop, //stop
		mediasoup_receive_video, //raw_video
		mediasoup_receive_audio, //raw_audio
		nullptr, //encoded_packet
		nullptr, //update
		mediasoup_stream_defaults, //get_defaults
		mediasoup_stream_properties, //get_properties
		nullptr, //unused1 (formerly pause)
		nullptr, //get_total_bytes
		nullptr, //get_dropped_frames
		nullptr, //type_data
		nullptr, //free_type_data
		nullptr, //get_congestion
		nullptr, //get_connect_time_ms
		"h264", //encoded_video_codecs
		"opus", //encoded_audio_codecs
		nullptr //raw_audio2
	};
#else
	struct obs_output_info mediasoup_output_info = {
		.id                   = "mediasoup_output",
		.flags                = OBS_OUTPUT_AV | OBS_OUTPUT_SERVICE,
		.get_name             = mediasoup_stream_getname,
		.create               = mediasoup_stream_create,
		.destroy              = mediasoup_stream_destroy,
		.start                = mediasoup_stream_start,
		.stop                 = mediasoup_stream_stop,
		.raw_video            = mediasoup_receive_video,
		.raw_audio            = mediasoup_receive_audio, // for single-track
		.encoded_packet       = nullptr,
		.update               = nullptr,
		.get_defaults         = mediasoup_stream_defaults,
		.get_properties       = mediasoup_stream_properties,
		.unused1              = nullptr,
		.get_total_bytes      = nullptr,
		.get_dropped_frames   = nullptr,
		.type_data            = nullptr,
		.free_type_data       = nullptr,
		.get_congestion       = nullptr,
		.get_connect_time_ms  = nullptr,
		.encoded_video_codecs = "h264",
		.encoded_audio_codecs = "opus",
		.raw_audio2           = nullptr
		// .raw_audio2           = mediasoup_receive_multitrack_audio, // for multi-track
	};
#endif
}
