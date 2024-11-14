#!/bin/bash

OPTIONS="x264 x265 svtav1 libvpx fdk-aac opus dav1d libvmaf aom"
DEPS=(
  "x264" 
  "x265" 
  "svtav1" 
  "libvpx" 
  "fdk-aac" 
  "opus" 
  "dav1d" 
  "libvmaf" 
  "aom" 
)

TARGET_DIR=$HOME/ffmpeg/build
BIN_DIR=$HOME/ffmpeg/bin

DEPS_BUILD_LOG=$HOME/ffmpeg/deps_build.log

if [[ " ${DEPS[*]} " =~ [[:space:]]x264[[:space:]] ]]; then
  echo "*** Building x264 ***"
  pushd $HOME/ffmpeg/x264
  make clean
  PATH="$HOME/ffmpeg/bin:$PATH" PKG_CONFIG_PATH="$HOME/ffmpeg_build/lib/pkgconfig" ./configure \
      --prefix="$HOME/ffmpeg/build" --bindir="$HOME/ffmpeg/bin" --enable-static --enable-pic > $DEPS_BUILD_LOG 2>&1 && \
  PATH="$HOME/ffmpeg/bin:$PATH" make -j$(nproc) > $DEPS_BUILD_LOG 2>&1 && \
  make install > $DEPS_BUILD_LOG 2>&1
  popd
fi


# x265
if [[ " ${DEPS[*]} " =~ [[:space:]]x265[[:space:]] ]]; then
  echo "*** Building x265 ***"
  pushd $HOME/ffmpeg/x265_git
  cd build/linux
  make clean
  find . -mindepth 1 ! -name 'make-Makefiles.bash' -and ! -name 'multilib.sh' -exec rm -rf {} +
  PATH="$HOME/ffmpeg/bin:$PATH" cmake -G "Unix Makefiles" \
      -DCMAKE_INSTALL_PREFIX="$TARGET_DIR" \
      -DENABLE_SHARED:BOOL=OFF             \
      -DSTATIC_LINK_CRT:BOOL=ON            \
      -DENABLE_CLI:BOOL=OFF ../../source > $DEPS_BUILD_LOG 2>&1
  sed -i 's/-lgcc_s/-lgcc_eh/g' x265.pc
  make -j $(nproc) > $DEPS_BUILD_LOG 2>&1
  make install > $DEPS_BUILD_LOG 2>&1
  popd
fi

# svtav1
if [[ " ${DEPS[*]} " =~ [[:space:]]svtav1[[:space:]] ]]; then
  echo "*** Building SVT-AV1 ***"
  pushd $HOME/ffmpeg/SVT-AV1
  rm -rf build
  mkdir build && cd build
  cmake .. -G"Unix Makefiles" \
      -DCMAKE_INSTALL_PREFIX="$TARGET_DIR" \
      -DCMAKE_BUILD_TYPE=Release           \
      -DBUILD_SHARED_LIBS=OFF              \
      -DCMAKE_INSTALL_PREFIX="$TARGET_DIR" > $DEPS_BUILD_LOG 2>&1
  make -j $(nproc) > $DEPS_BUILD_LOG 2>&1
  make install > $DEPS_BUILD_LOG 2>&1
  popd
fi


# libvpx
if [[ " ${DEPS[*]} " =~ [[:space:]]libvpx[[:space:]] ]]; then
  echo "*** Building libvpx ***"
  pushd $HOME/ffmpeg/libvpx
  make clean
  PATH="$BIN_DIR:$PATH" ./configure \
      --prefix="$TARGET_DIR" \
      --disable-examples --disable-unit-tests \
      --enable-vp9-highbitdepth --as=yasm --enable-pic  > $DEPS_BUILD_LOG 2>&1
  PATH="$BIN_DIR:$PATH" make -j$(nproc) > $DEPS_BUILD_LOG 2>&1
  make install > $DEPS_BUILD_LOG 2>&1
  popd
fi

# fdk-aac
if [[ " ${DEPS[*]} " =~ [[:space:]]fdk-aac[[:space:]] ]]; then
  echo "*** Building fdk-aac ***"
  pushd $HOME/ffmpeg/fdk-aac
  make clean
  autoreconf -fiv && \
  ./configure --prefix="$TARGET_DIR" --disable-shared > $DEPS_BUILD_LOG 2>&1 && \
  make -j$(nproc) > $DEPS_BUILD_LOG 2>&1 && \
  make install > $DEPS_BUILD_LOG 2>&1
  popd
fi

# opus
if [[ " ${DEPS[*]} " =~ [[:space:]]opus[[:space:]] ]]; then
  echo "*** Building opus ***"
  pushd $HOME/ffmpeg/opus
  make clean
  ./autogen.sh && \
  ./configure --prefix="$TARGET_DIR" --disable-shared > $DEPS_BUILD_LOG 2>&1 && \
  make -j$(nproc) > $DEPS_BUILD_LOG 2>&1 && \
  make install > $DEPS_BUILD_LOG 2>&1
  popd
fi


# libdav1d
if [[ " ${DEPS[*]} " =~ [[:space:]]dav1d[[:space:]] ]]; then
  echo "*** Building dav1d ***"
  pushd $HOME/ffmpeg/dav1d
  rm -rf build
  mkdir -p build && \
  cd build && \
  meson setup -Denable_tools=false -Denable_tests=false --default-library=static .. --prefix "$TARGET_DIR" --libdir="$TARGET_DIR/lib" > $DEPS_BUILD_LOG 2>&1 && \
  ninja > $DEPS_BUILD_LOG 2>&1 && \
  ninja install > $DEPS_BUILD_LOG 2>&1
  popd
fi

# libvmaf
if [[ " ${DEPS[*]} " =~ [[:space:]]libvmaf[[:space:]] ]]; then
  echo "*** Building libvmaf ***"
  pushd $HOME/ffmpeg/vmaf/
  rm -rf libvmaf/build
  cd libvmaf && mkdir -p build && \
  meson setup build -Denable_tests=false -Denable_docs=false --buildtype=release -Denable_avx512=true --default-library=static \
      --prefix="$TARGET_DIR" --bindir="$BIN_DIR" --libdir="$TARGET_DIR/lib" > $DEPS_BUILD_LOG 2>&1 && \
  ninja -vC build > $DEPS_BUILD_LOG 2>&1 && \
  ninja -vC build install > $DEPS_BUILD_LOG 2>&1
  popd
fi

# aom
if [[ " ${DEPS[*]} " =~ [[:space:]]aom[[:space:]] ]]; then
  echo "*** Building aom ***"
  pushd $HOME/ffmpeg/aom
  rm -rf aom_build
  mkdir -p aom_build && \
  cd aom_build && \
  PATH="$BIN_DIR:$PATH" cmake -G "Unix Makefiles" \
      -DCMAKE_INSTALL_PREFIX="$TARGET_DIR" \
      -DENABLE_TESTS=OFF \
      -DENABLE_NASM=on .. > $DEPS_BUILD_LOG 2>&1 && \
  PATH="$BIN_DIR:$PATH" make -j$(nproc) > $DEPS_BUILD_LOG 2>&1 && \
  make install > $DEPS_BUILD_LOG 2>&1
  popd
fi
