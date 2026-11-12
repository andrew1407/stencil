#!/bin/sh
# A static ffmpeg + ffprobe holding only what Stencil asks of them, for the cli, mcp and bot images:
# one video frame to PNG and a stream probe (cli/src/media/video.zig), an image shrunk to PNG (the
# bot's FfmpegImageDownscaler.cs). FFmpeg's own decoders plus dav1d and zlib only, so it is LGPL.
# Runs in an Alpine build stage; installs into $PREFIX (bin/ and share/licenses/).
set -eu

FFMPEG_VERSION=9.0.2
# Taken from the tarball whose ffmpeg.org signature (release key FCF986EA…D67658D8) verified.
FFMPEG_SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
DAV1D_VERSION=1.5.4
# As published beside the tarball on downloads.videolan.org.
DAV1D_SHA256=686616b7c69eb88d44459391ab25cac13b6647a3b288835c5784e71c1514a5c5
PREFIX=${PREFIX:-/opt/ffmpeg}

FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
DAV1D_URL="https://downloads.videolan.org/pub/videolan/dav1d/${DAV1D_VERSION}/dav1d-${DAV1D_VERSION}.tar.xz"

apk add --no-cache build-base nasm meson ninja pkgconf zlib-dev zlib-static curl xz

fetch() {   # url sha256 → unpacked into the working directory
  curl -fsSL "$1" -o src.tar.xz
  echo "$2  src.tar.xz" | sha256sum -c -
  tar -xJf src.tar.xz && rm src.tar.xz
}

mkdir -p /tmp/media && cd /tmp/media
fetch "$DAV1D_URL" "$DAV1D_SHA256"
cd "dav1d-${DAV1D_VERSION}"
meson setup build --prefix="$PREFIX" --libdir=lib --buildtype=release --default-library=static \
  -Denable_tools=false -Denable_tests=false
ninja -C build install
cd ..

fetch "$FFMPEG_URL" "$FFMPEG_SHA256"
cd "ffmpeg-${FFMPEG_VERSION}"
# Containers: the video extensions in common/config/mediaTypes.json, plus the images the bot shrinks.
CONFIG="--disable-everything --disable-autodetect --disable-network --disable-doc --disable-debug
  --disable-ffplay --disable-shared --enable-static --enable-small --enable-zlib --enable-libdav1d
  --enable-protocol=file,pipe
  --enable-demuxer=mov,matroska,avi,asf,flv,mpegps,mpegts,mpegvideo,ogg,image2,png_pipe,jpeg_pipe,webp_pipe,gif
  --enable-decoder=h264,hevc,vp8,vp9,libdav1d,mpeg4,msmpeg4v1,msmpeg4v2,msmpeg4v3,mpeg1video,mpeg2video,theora,wmv1,wmv2,wmv3,vc1,flv,h263,vp6,vp6f,mjpeg,prores,rawvideo,png,webp,gif
  --enable-parser=h264,hevc,vp8,vp9,av1,mpeg4video,mpegvideo,vc1,mjpeg,h263,png,webp,gif
  --enable-filter=select,scale,format,null,buffer,buffersink
  --enable-encoder=png --enable-muxer=image2,image2pipe"
# musl hands a thread 128 KiB of stack, and the MPEG-TS reader overruns it (a .ts segfaults on open):
# stack-size sets every thread's default to glibc's 2 MiB.
# shellcheck disable=SC2086 # CONFIG is a word list on purpose
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" ./configure --prefix="$PREFIX" --pkg-config-flags=--static \
  --extra-cflags="-I$PREFIX/include" --extra-ldflags="-L$PREFIX/lib -static" \
  --extra-ldexeflags="-static -Wl,-z,stack-size=2097152" $CONFIG
make -j"$(nproc)"
mkdir -p "$PREFIX/bin"
install -m 0755 ffmpeg ffprobe "$PREFIX/bin/"
strip "$PREFIX/bin/ffmpeg" "$PREFIX/bin/ffprobe"

# The LGPL's terms travel with the binaries: the licences, the exact sources and how they were built.
lic="$PREFIX/share/licenses"
mkdir -p "$lic/ffmpeg" "$lic/dav1d"
cp COPYING.LGPLv2.1 LICENSE.md "$lic/ffmpeg/"
cp "../dav1d-${DAV1D_VERSION}/COPYING" "$lic/dav1d/"
{
  echo "ffmpeg ${FFMPEG_VERSION} — source: ${FFMPEG_URL} (sha256 ${FFMPEG_SHA256}), unmodified"
  echo "dav1d ${DAV1D_VERSION} — source: ${DAV1D_URL} (sha256 ${DAV1D_SHA256}), unmodified"
  echo "ffmpeg configure flags:"
  echo "$CONFIG"
} > "$lic/SOURCES"
cd / && rm -rf /tmp/media
