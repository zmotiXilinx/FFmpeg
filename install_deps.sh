#!/bin/bash

OPTIONS="x264 x265 svtav1 libvpx fdk-aac opus dav1d libvmaf aom json-c"
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
  "json-c"
)

TARGET_DIR=$HOME/ffmpeg/build
BIN_DIR=$HOME/ffmpeg/bin

DEPS_BUILD_LOG=$HOME/ffmpeg/FFmpeg/deps_build.log

echo "" > $DEPS_BUILD_LOG

if [[ " ${DEPS[*]} " =~ [[:space:]]x264[[:space:]] ]]; then
  echo "*** Building x264 ***"
  pushd $HOME/ffmpeg/x264
  make clean
  PATH="$HOME/ffmpeg/bin:$PATH" PKG_CONFIG_PATH="$HOME/ffmpeg_build/lib/pkgconfig" ./configure \
      --prefix="$HOME/ffmpeg/build" --bindir="$HOME/ffmpeg/bin" --enable-static --enable-pic >> $DEPS_BUILD_LOG 2>&1 && \
  PATH="$HOME/ffmpeg/bin:$PATH" make -j$(nproc)                                              >> $DEPS_BUILD_LOG 2>&1 && \
  make install                                                                               >> $DEPS_BUILD_LOG 2>&1
  popd
fi


# x265
if [[ " ${DEPS[*]} " =~ [[:space:]]x265[[:space:]] ]]; then
  echo "*** Building x265 ***"
  pushd $HOME/ffmpeg/x265_git
  cd build/linux
  make clean
  find . -mindepth 1 ! -name 'make-Makefiles.bash' -and ! -name 'multilib.sh' -exec rm -rf {} +
  mkdir -p 10bit
  mkdir -p 8bit
  pushd 10bit
  PATH="$HOME/ffmpeg/bin:$PATH" cmake -G "Unix Makefiles" \
      -DCMAKE_INSTALL_PREFIX="$TARGET_DIR" \
      -DHIGH_BIT_DEPTH=ON                  \
      -DENABLE_SHARED=OFF                  \
      -DSTATIC_LINK_CRT=ON                 \
      -DENABLE_HDR10_PLUS=ON               \
      -DENABLE_CLI=OFF                     \
      -DEXPORT_C_API=OFF  ../../../source                                    >> $DEPS_BUILD_LOG 2>&1
  sed -i 's/-lgcc_s/-lgcc_eh/g' x265.pc
  make -j $(nproc)                                                           >> $DEPS_BUILD_LOG 2>&1
  popd

  pushd 8bit
  ln -sf ../10bit/libx265.a libx265_main10.a
  PATH="$HOME/ffmpeg/bin:$PATH" cmake -G "Unix Makefiles" \
      -DCMAKE_INSTALL_PREFIX="$TARGET_DIR" \
      -DENABLE_SHARED=OFF                  \
      -DSTATIC_LINK_CRT=ON                 \
      -DENABLE_HDR10_PLUS=ON               \
      -DENABLE_CLI=OFF                     \
      -DEXTRA_LIB="x265_main10.a"          \
      -DEXTRA_LINK_FLAGS=-L.               \
      -DLINKED_10BIT=ON  ../../../source                                     >> $DEPS_BUILD_LOG 2>&1
  sed -i 's/-lgcc_s/-lgcc_eh/g' x265.pc
  make -j $(nproc)                                                           >> $DEPS_BUILD_LOG 2>&1
  popd

  ln -sf ./10bit/libx265.a libx265_main10.a
  ln -sf ./8bit/libx265.a libx265_main.a
ar -M <<EOF
CREATE libx265.a
ADDLIB libx265_main.a
ADDLIB libx265_main10.a
SAVE
END
EOF
  cp libx265.a "$TARGET_DIR/lib"
  cp 8bit/x265.pc "$TARGET_DIR/lib/pkgconfig"
  cp 8bit/x265_config.h "$TARGET_DIR/include"
  cp ../../source/x265.h "$TARGET_DIR/include"
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
      -DCMAKE_INSTALL_PREFIX="$TARGET_DIR"                                   >> $DEPS_BUILD_LOG 2>&1
  make -j $(nproc)                                                           >> $DEPS_BUILD_LOG 2>&1
  make install                                                               >> $DEPS_BUILD_LOG 2>&1
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
      --enable-vp9-highbitdepth --as=yasm --enable-pic                       >> $DEPS_BUILD_LOG 2>&1
  PATH="$BIN_DIR:$PATH" make -j$(nproc)                                      >> $DEPS_BUILD_LOG 2>&1
  make install                                                               >> $DEPS_BUILD_LOG 2>&1
  popd
fi

# fdk-aac
if [[ " ${DEPS[*]} " =~ [[:space:]]fdk-aac[[:space:]] ]]; then
  echo "*** Building fdk-aac ***"
  pushd $HOME/ffmpeg/fdk-aac
  make clean
  autoreconf -fiv && \
  ./configure --prefix="$TARGET_DIR" --disable-shared                        >> $DEPS_BUILD_LOG 2>&1 && \
  make -j$(nproc)                                                            >> $DEPS_BUILD_LOG 2>&1 && \
  make install                                                               >> $DEPS_BUILD_LOG 2>&1
  popd
fi

# opus
if [[ " ${DEPS[*]} " =~ [[:space:]]opus[[:space:]] ]]; then
  echo "*** Building opus ***"
  pushd $HOME/ffmpeg/opus
  make clean
  ./autogen.sh && \
  ./configure --prefix="$TARGET_DIR" --disable-shared                        >> $DEPS_BUILD_LOG 2>&1 && \
  make -j$(nproc)                                                            >> $DEPS_BUILD_LOG 2>&1 && \
  make install                                                               >> $DEPS_BUILD_LOG 2>&1
  popd
fi


# libdav1d
if [[ " ${DEPS[*]} " =~ [[:space:]]dav1d[[:space:]] ]]; then
  echo "*** Building dav1d ***"
  pushd $HOME/ffmpeg/dav1d
  rm -rf build
  mkdir -p build && \
  cd build && \
  meson setup -Denable_tools=false \
              -Denable_tests=false \
              --default-library=static .. \
              --prefix "$TARGET_DIR" \
              --libdir="$TARGET_DIR/lib"                                     >> $DEPS_BUILD_LOG 2>&1 && \
  ninja                                                                      >> $DEPS_BUILD_LOG 2>&1 && \
  ninja install                                                              >> $DEPS_BUILD_LOG 2>&1
  popd
fi

# libvmaf
if [[ " ${DEPS[*]} " =~ [[:space:]]libvmaf[[:space:]] ]]; then
  echo "*** Building libvmaf ***"
  pushd $HOME/ffmpeg/vmaf/
  rm -rf libvmaf/build
  cd libvmaf && mkdir -p build && \
  meson setup build -Denable_tests=false \
                    -Denable_docs=false \
                    --buildtype=release \
                    -Denable_avx512=true \
                    --default-library=static \
                    --prefix="$TARGET_DIR" \
                    --bindir="$BIN_DIR" \
                    --libdir="$TARGET_DIR/lib"                               >> $DEPS_BUILD_LOG 2>&1 && \
  ninja -vC build                                                            >> $DEPS_BUILD_LOG 2>&1 && \
  ninja -vC build install                                                    >> $DEPS_BUILD_LOG 2>&1
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
      -DENABLE_NASM=on ..                                                    >> $DEPS_BUILD_LOG 2>&1 && \
  PATH="$BIN_DIR:$PATH" make -j$(nproc)                                      >> $DEPS_BUILD_LOG 2>&1 && \
  make install                                                               >> $DEPS_BUILD_LOG 2>&1
  popd
fi


# json-c
if [[ " ${DEPS[*]} " =~ [[:space:]]json-c[[:space:]] ]]; then
  echo "*** Building json-c ***"
  pushd $HOME/ffmpeg/json-c
  rm -rf json-c-build
  mkdir -p json-c-build && \
  cd json-c-build && \
  cmake -DCMAKE_INSTALL_PREFIX="$TARGET_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_STATIC_LIBS=ON ..                                            >> $DEPS_BUILD_LOG 2>&1 && \
  make -j$(nproc)                                                            >> $DEPS_BUILD_LOG 2>&1 && \
  make install                                                               >> $DEPS_BUILD_LOG 2>&1
  popd
fi
