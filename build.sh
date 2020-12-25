set -e
set -x

g++ -std=c++11 -O3 audio_convert.cc -o bin/audio_convert -DWITH_MAIN \
  -I /Users/smarsu/tencent/projects/audio_crash/FFmpeg/x86/include \
  /Users/smarsu/tencent/projects/audio_crash/FFmpeg/x86/lib/*.a

g++ -std=c++11 -O3 audio_volume.cc -o bin/audio_volume -DWITH_MAIN \
  -I /Users/smarsu/tencent/projects/audio_crash/FFmpeg/x86/include \
  /Users/smarsu/tencent/projects/audio_crash/FFmpeg/x86/lib/*.a

g++ -std=c++11 -O3 media_duration.cc -o bin/media_duration -DWITH_MAIN \
  -I /Users/smarsu/tencent/projects/audio_crash/FFmpeg/x86/include \
  /Users/smarsu/tencent/projects/audio_crash/FFmpeg/x86/lib/*.a

g++ -std=c++11 -O3 audio_cut.cc -o bin/audio_cut -DWITH_MAIN \
  -I /Users/smarsu/tencent/projects/audio_crash/FFmpeg/x86/include \
  /Users/smarsu/tencent/projects/audio_crash/FFmpeg/x86/lib/*.a

rm -rf build_android
mkdir -p build_android
cd build_android

cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI="arm64-v8a" -DANDROID_ARM_NEON=ON \
    -DANDROID_PLATFORM=android-21 .. -DCMAKE_BUILD_TYPE=RELEASE
make -j 32

cd ..

cp build_android/libaudio_crash.a ~/tencent/projects/flutter_audio_crash/android/arm64/
