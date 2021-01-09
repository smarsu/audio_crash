set -e
set -x

# ./build-ffmpeg.sh $1

g++ -std=c++11 -O3 audio_convert.cc -o bin/audio_convert -DWITH_MAIN \
  -I /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/include \
  /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/lib/*.a

g++ -std=c++11 -O3 audio_volume.cc -o bin/audio_volume -DWITH_MAIN \
  -I /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/include \
  /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/lib/*.a

g++ -std=c++11 -O3 media_duration.cc -o bin/media_duration -DWITH_MAIN \
  -I /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/include \
  /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/lib/*.a

g++ -std=c++11 -O3 audio_cut.cc -o bin/audio_cut -DWITH_MAIN \
  -I /Users/smarsu/tencent/ffmpeg-4.2/x86/include \
  /Users/smarsu/tencent/ffmpeg-4.2/x86/lib/*.a

g++ -std=c++11 -O3 bleu.cc -o bin/bleu -DWITH_MAIN \
  -I /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/include \
  /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/lib/*.a

g++ -std=c++11 -O3 video_thumbnail.cc -o bin/video_thumbnail -DWITH_MAIN \
  -I /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/include \
  /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/lib/*.a

g++ -std=c++11 -O3 edit_pass.cc -o bin/edit_pass -DWITH_MAIN \
  -I /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/include \
  /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/lib/*.a

g++ -std=c++11 -O3 audio_gradient.cc -o bin/audio_gradient -DWITH_MAIN \
  -I /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/include \
  /Users/smarsu/tencent/projects/audio_crash/ffmpeg-4.2/x86/lib/*.a \
  -I /Users/smarsu/tencent/projects/audio_crash/smcc/core \
  /Users/smarsu/tencent/projects/audio_crash/smcc/build/core/libsmcc_core.a

rm -rf build_ios
mkdir -p build_ios
cd build_ios

cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain/iOS.cmake -DIOS_PLATFORM=OS -DCMAKE_OSX_ARCHITECTURES=$1 \
  -DMACOSX_DEPLOYMENT_TARGET=8.0
make -j 32

# codesign -f -s "Apple Development: 754247874@qq.com" libaudio_crash.dylib
cd ..

cp build_ios/libaudio_crash.a /Users/smarsu/tencent/projects/flutter_audio_crash/ios/Vendored/
# cp build_ios/libaudio_crash.dylib ~/tencent/projects/flutter_audio_crash/ios/Classes/libaudio_crash.dylib
# cp build_ios/libaudio_crash.dylib /Users/smarsu/tencent/projects/video_editor_flutter/ios/Runner/frameworks/libaudio_crash.dylib

rm -rf build_android
mkdir -p build_android
cd build_android

cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI="arm64-v8a" -DANDROID_ARM_NEON=ON \
    -DANDROID_PLATFORM=android-21 .. -DCMAKE_BUILD_TYPE=RELEASE
make -j 32

cd ..

cp build_android/libaudio_crash.a ~/tencent/projects/flutter_audio_crash/android/arm64/
cp build_android/smcc/core/libsmcc_core.a ~/tencent/projects/flutter_audio_crash/android/arm64/
