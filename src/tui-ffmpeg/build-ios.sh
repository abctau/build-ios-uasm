#!/bin/bash
# ffmpeg 7.1.1 minimal LGPL build for iOS - run on macOS (CI or any Mac with Xcode).
# Produces static libs for iphoneos(arm64) + iphonesimulator(arm64,x86_64), then
# merges the 6 libs into one fat libUasmFfmpegLibs.a per platform for xcframework use.
# Usage: bash build-ios.sh
set -e

FF_VER='7.1.1'
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FF_SRC="${FF_SRC:-$SCRIPT_DIR/../../ffmpeg-spike-src/ffmpeg-$FF_VER}"
OUT_BASE="$SCRIPT_DIR/vendor/ffmpeg/lib/ios"

if [ ! -f "$FF_SRC/configure" ]; then
  echo "downloading ffmpeg $FF_VER source..."
  mkdir -p "$(dirname "$FF_SRC")"
  curl -L -o /tmp/ffmpeg-$FF_VER.tar.xz "https://ffmpeg.org/releases/ffmpeg-$FF_VER.tar.xz"
  tar xf "/tmp/ffmpeg-$FF_VER.tar.xz" -C "$(dirname "$FF_SRC")"
fi

DEPLOY='15.0'

build_one() {
  local SDK=$1 MIN_FLAG=$2 ARCH=$3 EXTRA_ARCH=$4 BUILD=$5
  local SYSROOT
  SYSROOT=$(xcrun --sdk "$SDK" --show-sdk-path)
  mkdir -p "$BUILD"
  cd "$BUILD"
  if [ -f config.mak ]; then
    make distclean >/dev/null 2>&1 || true
  fi
  "$FF_SRC/configure" \
    --target-os=darwin \
    --arch="$ARCH" \
    $EXTRA_ARCH \
    --enable-cross-compile \
    --sysroot="$SYSROOT" \
    --cc="xcrun --sdk $SDK clang" \
    --cxx="xcrun --sdk $SDK clang++" \
    --extra-cflags="-arch $ARCH -isysroot $SYSROOT $MIN_FLAG" \
    --extra-ldflags="-arch $ARCH -isysroot $SYSROOT $MIN_FLAG" \
    --host-cc=clang \
    --disable-everything \
    --enable-swscale \
    --enable-demuxer=mov,mp4,m4a,3gp,matroska,webm,avi,flv,mp3,wav,ogg,aac,flac,image2,image2pipe \
    --enable-decoder=h264,hevc,mpeg4,vp8,vp9,aac,mp3,flac,pcm_s16le,png,mjpeg \
    --enable-parser=h264,hevc,mpeg4video,vp8,vp9,aac,mpegaudio,flac \
    --enable-protocol=file \
    --enable-encoder=aac,mpeg4,png \
    --enable-muxer=mp4,ipod,adts,image2 \
    --enable-filter=crop,scale,overlay,format,setpts,hue,eq,null,negate,atempo,amix,volume,aformat,anull,aresample \
    --disable-programs --disable-doc \
    --disable-avdevice --disable-postproc \
    --disable-network \
    --disable-sdl2 --disable-xlib --disable-zlib --disable-lzma --disable-bzlib \
    --disable-iconv --disable-vulkan \
    --disable-shared --enable-static --disable-debug \
    --disable-autodetect
  make -j"$(sysctl -n hw.ncpu)"
  echo "BUILD OK: $BUILD"
}

# 1. iphoneos arm64 (device)
build_one iphoneos "-miphoneos-version-min=$DEPLOY" arm64 "" "$FF_SRC/build-ios-iphoneos"

# 2. iphonesimulator arm64
build_one iphonesimulator "-mios-simulator-version-min=$DEPLOY" arm64 "" "$FF_SRC/build-ios-sim-arm64"

# 3. iphonesimulator x86_64
build_one iphonesimulator "-mios-simulator-version-min=$DEPLOY" x86_64 "--disable-x86asm" "$FF_SRC/build-ios-sim-x64"

LIBS="libavfilter.a libswresample.a libavformat.a libavcodec.a libswscale.a libavutil.a"

# static libs land in per-lib subdirs (e.g. build-ios-iphoneos/libavfilter/libavfilter.a)
lib_path() {
  local BUILD=$1 L=$2
  echo "$BUILD/${L%.a}/$L"
}

# 4. device libs (arm64)
mkdir -p "$OUT_BASE/iphoneos"
for L in $LIBS; do
  cp "$(lib_path "$FF_SRC/build-ios-iphoneos" "$L")" "$OUT_BASE/iphoneos/$L"
done
# simulator fat (arm64 + x86_64 via lipo)
mkdir -p "$OUT_BASE/iphonesimulator"
for L in $LIBS; do
  lipo -create "$(lib_path "$FF_SRC/build-ios-sim-arm64" "$L")" "$(lib_path "$FF_SRC/build-ios-sim-x64" "$L")" \
    -output "$OUT_BASE/iphonesimulator/$L"
done

# 5. single merged lib for final app link: libUasmFfmpegLibs.a per slot
mkdir -p "$OUT_BASE/iphoneos" "$OUT_BASE/iphonesimulator"
xcrun libtool -static -o "$OUT_BASE/iphoneos/libUasmFfmpegLibs.a" $(for L in $LIBS; do echo "$OUT_BASE/iphoneos/$L"; done)
xcrun libtool -static -o "$OUT_BASE/iphonesimulator/libUasmFfmpegLibs.a" $(for L in $LIBS; do echo "$OUT_BASE/iphonesimulator/$L"; done)

ls -l "$OUT_BASE/iphoneos" "$OUT_BASE/iphonesimulator"
echo "ALL OK: $OUT_BASE"
