project(obs-outputs)

if (POLICY CMP0063)
   # This policy tells cmake to really honor CXX_VISIBILITY_PRESET rather than
   # ignore it for backwards compatibility.
   cmake_policy(SET CMP0063 NEW)
endif()

set(WITH_RTMPS AUTO CACHE STRING "Enable RTMPS support with mbedTLS")
set_property(CACHE WITH_RTMPS PROPERTY STRINGS AUTO ON OFF)

option(STATIC_MBEDTLS "Statically link mbedTLS into binary" ON)

if (WITH_RTMPS STREQUAL "AUTO")
	find_package(MbedTLS)
	find_package(ZLIB)
	if (NOT MBEDTLS_FOUND OR NOT ZLIB_FOUND)
		set(WITH_RTMPS "OFF")
		message(WARNING "mbedTLS or zlib was not found, RTMPS will be auto-disabled")
	endif()
endif()

if (WITH_RTMPS)
	find_package(MbedTLS REQUIRED)
	find_package(ZLIB REQUIRED)
	add_definitions(-DCRYPTO -DUSE_MBEDTLS)
	include_directories(${MBEDTLS_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})
else()
	add_definitions(-DNO_CRYPTO)
endif()


set(COMPILE_FTL FALSE)

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/obs-outputs-config.h.in"
	"${CMAKE_BINARY_DIR}/plugins/obs-outputs/config/obs-outputs-config.h")

include_directories("${CMAKE_BINARY_DIR}/plugins/obs-outputs/config")

#------------------------------------------------------------------------
# WebRTC
#
if(WIN32)
	set(WEBRTC_USE_FILE_INCLUDED 0)
endif()


message( STATUS "Looking for libwebrtc" )
find_package(LibWebRTC REQUIRED)
message( STATUS "Looking for libwebrtc - found" )


message(STATUS "WEBRTC_INCLUDE_DIR : " ${WEBRTC_INCLUDE_DIR})
include_directories(${WEBRTC_INCLUDE_DIRS})

set(LIBWEBRTC_INCLUDE_PATH ${WEBRTC_INCLUDE_DIR} CACHE STRING "libwebrtc include path")
get_filename_component(WEBRTC_BINARY_PATH ${LIBWEBRTC_LIBRARY} DIRECTORY)
set(LIBWEBRTC_BINARY_PATH ${WEBRTC_BINARY_PATH} CACHE STRING "libwebrtc binary path")


if(WIN32)
	set(obs-outputs_PLATFORM_DEPS
		ws2_32
		winmm
		Iphlpapi)

	if (WITH_RTMPS OR (WITH_RTMPS STREQUAL "AUTO"))
		SET(obs-outputs_PLATFORM_DEPS
			${obs-outputs_PLATFORM_DEPS}
			crypt32)
	endif()
endif()

if(MSVC)
	set(obs-outputs_PLATFORM_DEPS
		${obs-outputs_PLATFORM_DEPS}
		w32-pthreads)
endif()

if(APPLE AND (WITH_RTMPS OR (WITH_RTMPS STREQUAL "AUTO")))
	find_library(FOUNDATION_FRAMEWORK Foundation)
	find_library(SECURITY_FRAMEWORK Security)

	set(obs-outputs_PLATFORM_DEPS
		${obs-outputs_PLATFORM_DEPS}
		${FOUNDATION_FRAMEWORK}
		${SECURITY_FRAMEWORK})
endif()

set(obs-outputs_librtmp_HEADERS
	librtmp/amf.h
	librtmp/bytes.h
	librtmp/cencode.h
	librtmp/dh.h
	librtmp/dhgroups.h
	librtmp/handshake.h
	librtmp/http.h
	librtmp/log.h
	librtmp/md5.h
	librtmp/rtmp.h
	librtmp/rtmp_sys.h)
set(obs-outputs_librtmp_SOURCES
	librtmp/amf.c
	librtmp/cencode.c
	librtmp/hashswf.c
	librtmp/log.c
	# librtmp/md5.c
	librtmp/parseurl.c
	librtmp/rtmp.c)

if(NOT WIN32)
	set_source_files_properties(${obs-outputs_librtmp_SOURCES} PROPERTIES
		COMPILE_FLAGS "-fvisibility=hidden")
endif()

set(obs-outputs_webrtc_HEADERS
	AudioDeviceModuleWrapper.h
	obsWebrtcAudioSource.h
	VideoCapturer.h
	# Broadcaster.h
	WebRTCStream.h)
set(obs-outputs_webrtc_SOURCES
	AudioDeviceModuleWrapper.cpp
	obsWebrtcAudioSource.cpp
	VideoCapturer.cpp
	# Broadcaster.cpp
	WebRTCStream.cpp)

if(NOT WIN32)
	set_source_files_properties(${obs-outputs_webrtc_SOURCES} PROPERTIES
		COMPILE_FLAGS "-fvisibility=hidden")
endif()

set(obs-outputs_HEADERS
	"${CMAKE_BINARY_DIR}/plugins/obs-outputs/config/obs-outputs-config.h"
	obs-output-ver.h
	rtmp-helpers.h
	rtmp-stream.h
	net-if.h
	flv-mux.h)
set(obs-outputs_SOURCES
	obs-outputs.c
	null-output.c
	rtmp-stream.c
	rtmp-windows.c
	flv-output.c
	flv-mux.c
	net-if.c)

if(WIN32)
	set(MODULE_DESCRIPTION "OBS output module")
	configure_file(${CMAKE_SOURCE_DIR}/cmake/winrc/obs-module.rc.in obs-outputs.rc)
	list(APPEND obs-outputs_SOURCES
		obs-outputs.rc)
endif()


add_subdirectory(libmediasoupclient)
set_target_properties(mediasoupclient PROPERTIES CXX_VISIBILITY_PRESET hidden)
# set_target_properties(test_mediasoupclient PROPERTIES CXX_VISIBILITY_PRESET hidden)
# set_target_properties(webrtc PROPERTIES CXX_VISIBILITY_PRESET hidden)
set_target_properties(sdptransform PROPERTIES CXX_VISIBILITY_PRESET hidden)
set_target_properties(test_sdptransform PROPERTIES CXX_VISIBILITY_PRESET hidden)

# add_subdirectory(libwebrtc)
# set_target_properties(webrtc_broadcaster PROPERTIES CXX_VISIBILITY_PRESET hidden)

add_subdirectory(cpr)

message("\n=========== STUFF ===========\n")
message(STATUS "LIBWEBRTC_INCLUDE_PATH                : " ${LIBWEBRTC_INCLUDE_PATH})
message(STATUS "LIBWEBRTC_BINARY_PATH                 : " ${LIBWEBRTC_BINARY_PATH})
message(STATUS "WEBRTC_INCLUDE_DIRS                   : " ${WEBRTC_INCLUDE_DIRS})
message(STATUS "OPENSSL_INCLUDE_DIR                   : " ${OPENSSL_INCLUDE_DIR})
message(STATUS "CMAKE_USE_OPENSSL                     : " ${CMAKE_USE_OPENSSL})
message(STATUS "CPR_INCLUDE_DIRS                      : " ${CPR_INCLUDE_DIRS})
message(STATUS "CPR_LIBRARIES                         : " ${CPR_LIBRARIES})
message(STATUS "WEBRTC_LIBRARIES                      : " ${WEBRTC_LIBRARIES})
message("")


add_library(obs-outputs MODULE
	${ftl_SOURCES}
	${ftl_HEADERS}
	${obs-outputs_SOURCES}
	${obs-outputs_HEADERS}
	${obs-outputs_librtmp_SOURCES}
	${obs-outputs_librtmp_HEADERS}
	${obs-outputs_webrtc_SOURCES}
	${obs-outputs_webrtc_HEADERS})

# Public (interface) headers from dependencies.
target_include_directories(obs-outputs PUBLIC
	${CPR_INCLUDE_DIRS}
	"${CMAKE_CURRENT_LIST_DIR}/libmediasoupclient/include"
)

target_link_libraries(obs-outputs
	${WEBRTC_LIBRARIES}
	libobs
	mediasoupclient
	${CPR_LIBRARIES}
	${MBEDTLS_LIBRARIES}
	${ZLIB_LIBRARIES}
	${ftl_IMPORTS}
	${obs-outputs_PLATFORM_DEPS})
set_target_properties(obs-outputs PROPERTIES FOLDER "plugins")

install_obs_plugin_with_data(obs-outputs data)
