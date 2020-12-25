set -e
set -x

./configure \
  --prefix=x86 \
  --disable-debug --disable-programs --disable-doc --disable-nonfree --disable-audiotoolbox --disable-videotoolbox --disable-appkit --disable-bzlib --disable-iconv --disable-securetransport --disable-avfoundation --disable-coreimage --disable-lzma --disable-zlib \

make clean
make -j 32
make install
