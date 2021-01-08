# Copyright (c) 2020 smarsufan. All Rights Reserved.

cmake_minimum_required(VERSION 3.6.0)
project(audio_crash)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -O3")

if (IOS)
  set(FFMPEG_HOME "thin/arm64")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mios-version-min=${MACOSX_DEPLOYMENT_TARGET} -fembed-bitcode")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mios-version-min=${MACOSX_DEPLOYMENT_TARGET} -fembed-bitcode")
elseif(ANDROID)
  set(FFMPEG_HOME "/Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/arm64")
endif()

include_directories(${FFMPEG_HOME}/include)
link_directories(${FFMPEG_HOME}/lib)

include_directories(smcc/core)
add_subdirectory(smcc/core)

set(SRC audio_convert.cc audio_volume.cc media_duration.cc audio_cut.cc bleu.cc video_thumbnail.cc edit_pass.cc audio_gradient.cc)

if (IOS)
  add_library(audio_crash STATIC ${SRC})
elseif(ANDROID)
  add_library(audio_crash STATIC ${SRC})
endif()

target_link_libraries(
  audio_crash 
  libavformat.a libavcodec.a libavfilter.a libavutil.a libswresample.a libswscale.a
  smcc_core
)
