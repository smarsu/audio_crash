# Copyright (c) 2020 smarsufan. All Rights Reserved.

cmake_minimum_required(VERSION 3.6.0)
project(audio_crash)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O3")

if (IOS)
elseif(ANDROID)
  set(FFMPEG_HOME "/Users/smarsu/tencent/projects/audio_crash/FFmpeg/arm64")
endif()

include_directories(${FFMPEG_HOME}/include)
link_directories(${FFMPEG_HOME}/lib)

set(SRC audio_convert.cc audio_volume.cc)

if (IOS)
  add_library(audio_crash SHARED ${SRC})
elseif(ANDROID)
  add_library(audio_crash STATIC ${SRC})
endif()

target_link_libraries(
  audio_crash 
  libavformat.a libavcodec.a libavfilter.a libavutil.a libswresample.a libswscale.a
)
