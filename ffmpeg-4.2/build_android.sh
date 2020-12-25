set -e
set -x

./configure \
  --prefix=arm64 \
  --enable-cross-compile --disable-debug --disable-programs --disable-doc --disable-nonfree --disable-audiotoolbox --disable-videotoolbox --disable-appkit --disable-bzlib --disable-iconv --disable-securetransport --disable-avfoundation --disable-coreimage --disable-lzma --disable-zlib \
  --cross-prefix=/Users/smarsu/tencent/projects/android-sdk-macosx/ndk/20.0.5594570/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android- \
  --target-os=android \
  --arch=arm64 \
  --sysroot=/Users/smarsu/tencent/projects/android-sdk-macosx/ndk/20.0.5594570/platforms/android-21/arch-arm64 \
  --extra-cflags="-I/Users/smarsu/tencent/projects/android-sdk-macosx/ndk/20.0.5594570/sysroot/usr/include/aarch64-linux-android/ -I/Users/smarsu/tencent/projects/android-sdk-macosx/ndk/20.0.5594570/sysroot/usr/include/ -O3"

make clean
make -j 32
make install
