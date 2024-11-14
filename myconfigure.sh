# #!/bin/bash

# for localdev
if [[ $1 == "" || $1 == "dev" ]]; then
PATH="$HOME/bin:$PATH" PKG_CONFIG_PATH="$HOME/ffmpeg/build/lib/pkgconfig" ./configure   \
  --prefix="$HOME/ffmpeg/build" \
  --pkg-config-flags="--static"   \
  --extra-cflags="-I$HOME/ffmpeg/build/include"   \
  --extra-ldflags="-L$HOME/ffmpeg/build/lib"   \
  --extra-libs="-lpthread -lm"   \
  --ld="g++"   \
  --bindir="$HOME/ffmpeg/bin"   \
  --enable-gpl   \
  --enable-libaom   \
  --enable-libfdk-aac   \
  --enable-libopus   \
  --enable-libsvtav1   \
  --enable-libdav1d   \
  --enable-libvpx   \
  --enable-libx264   \
  --enable-libx265   \
  --enable-nonfree \
  --disable-sndio \
  --disable-optimizations \
  --disable-stripping \
  --disable-doc \
  --disable-ffplay \
  --disable-ffprobe
fi


if [[ $1 == "prod" ]]; then
  echo "prod configure"
  PATH="$HOME/bin:$PATH" PKG_CONFIG_PATH="$HOME/ffmpeg/build/lib/pkgconfig" ./configure   \
    --prefix="$HOME/ffmpeg/build" \
    --pkg-config-flags="--static"   \
    --extra-cflags="-I$HOME/ffmpeg/build/include"   \
    --extra-ldflags="-L$HOME/ffmpeg/build/lib"      \
    --extra-libs="-lpthread -lm -lz"                \
    --ld="g++"                                      \
    --bindir="$HOME/ffmpeg/bin"                     \
    --enable-gpl                                    \
    --enable-pic                                    \
    --enable-static                                 \
    --disable-shared                                \
    --enable-libx264                                \
    --enable-libx265                                \
    --enable-libsvtav1                              \
    --enable-libvpx                                 \
    --enable-libfdk-aac                             \
    --enable-libopus                                \
    --enable-libdav1d                               \
    --enable-libvmaf                                \
    --enable-libaom                                 \
    --enable-nonfree                                \
    --disable-doc

  if [[ $? -ne 0 ]]; then
    echo "configure failed"
    exit 1
  fi

  make -j$(nproc)

  rm -rf staticx
  mkdir -p staticx
  pushd staticx
  staticx ../ffmpeg ffmpeg
  staticx ../ffmpeg_g ffmpeg_g
  staticx ../ffprobe_g ffprobe_g
  staticx ../ffprobe ffprobe
  popd
fi