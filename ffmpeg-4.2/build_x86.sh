set -e
set -x

rm -rf x86

./configure \
  --prefix=x86 \
  --disable-debug --disable-programs --disable-doc --disable-nonfree --disable-audiotoolbox --disable-videotoolbox --disable-appkit --disable-bzlib --disable-iconv --disable-securetransport --disable-avfoundation --disable-coreimage --disable-lzma --disable-zlib \

# ./configure \
# --prefix=x86 \
# --disable-everything \
# --disable-all \
# --enable-avformat \
# --enable-avcodec \
# --enable-swresample \
# --disable-network \
# --disable-autodetect \
# --enable-small \
# --enable-decoder=aac \
# --enable-demuxer=mov \
# --enable-muxer=mp4 \
# --enable-encoder=aac \
# --enable-protocol=file \
# --enable-filter=aresample \

make clean
make -j 32
make install
