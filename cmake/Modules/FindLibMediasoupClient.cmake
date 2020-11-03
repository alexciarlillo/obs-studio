# Once done these will be defined:
#
#  LIBMEDIASOUPCLIENT_FOUND
#  LIBMEDIASOUPCLIENT_INCLUDE_DIRS
#  LIBMEDIASOUPCLIENT_LIBRARIES
#

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
	pkg_check_modules(_MEDIASOUPCLIENT QUIET LibMediasoupClient)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_lib_suffix 64)
else()
	set(_lib_suffix 32)
endif()

find_path(MEDIASOUPCLIENT_INCLUDE_DIR
	NAMES
		mediasoupclient.hpp
	HINTS
		${MEDIASOUP_CLIENT_DIR}/include
)

find_library(MEDIASOUPCLIENT_LIB
	NAMES libmediasoupclient.a
	HINTS
		${MEDIASOUP_CLIENT_DIR}/build
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibMediasoupClient DEFAULT_MSG MEDIASOUPCLIENT_LIB MEDIASOUPCLIENT_INCLUDE_DIR)
mark_as_advanced(MEDIASOUPCLIENT_INCLUDE_DIR MEDIASOUPCLIENT_LIB)


if(LIBMEDIASOUPCLIENT_FOUND)
	set(LIBMEDIASOUPCLIENT_INCLUDE_DIRS ${MEDIASOUPCLIENT_INCLUDE_DIR})
	set(LIBMEDIASOUPCLIENT_LIBRARIES ${MEDIASOUPCLIENT_LIB})

	message(STATUS "MEDIASOUP_CLIENT_DIR: ${MEDIASOUP_CLIENT_DIR}")
	message(STATUS "LIBMEDIASOUPCLIENT_INCLUDE_DIRS:")
	foreach(_dir ${LIBMEDIASOUPCLIENT_INCLUDE_DIRS})
		message(STATUS "-- ${_dir}")
	endforeach()
	message(STATUS "LIBMEDIASOUPCLIENT_LIBRARIES: ${LIBMEDIASOUPCLIENT_LIBRARIES}")
endif()
