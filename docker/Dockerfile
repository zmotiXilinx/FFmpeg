FROM ubuntu:20.04

# zohaibm' userid and groupid (on VMs:102894 and GID:10604)
ARG USER_ID=5000
ARG GROUP_ID=5000
ARG GROUP_NAME=zohaibm
ARG MAPPED_USER=mapped

# set timezone or else apt will ask for it interactively and docker-build will fail
RUN apt-get update && \
    apt-get install -yq tzdata && \
    ln -fs /usr/share/zoneinfo/America/New_York /etc/localtime && \
    dpkg-reconfigure -f noninteractive tzdata

RUN apt update
RUN apt install -y vim sudo

# ffmpeg 
RUN apt update -qq && apt -y install \
  autoconf \
  automake \
  build-essential \
  cmake \
  git \
  meson \
  ninja-build \
  pkg-config \
  texinfo \
  wget \
  yasm \
  nasm \
  libtool \
  zlib1g-dev

# required for meson
RUN apt -y install libncurses5-dev libgdbm-dev libnss3-dev libssl-dev libreadline-dev libffi-dev
RUN cd /tmp; \
    wget https://www.python.org/ftp/python/3.12.6/Python-3.12.6.tgz; \
    tar -xvf Python-3.12.6.tgz && cd Python-3.12.6; \
    ./configure --enable-optimizations --disable-test-modules && \
    make -j$(nproc) && make install;

RUN pip3 install meson

RUN if getent group ${GROUP_NAME} ; then groupdel ${GROUP_NAME}; fi &&\
groupadd -g ${GROUP_ID} ${GROUP_NAME} &&\
useradd -l -u ${USER_ID} -g ${GROUP_NAME} --home /home/${MAPPED_USER} ${MAPPED_USER}

RUN mkdir /home/${MAPPED_USER} && chown mapped:${GROUP_NAME} /home/${MAPPED_USER}

ADD ./docker.bashrc /etc/docker.bashrc
RUN <<EOF
cat /etc/docker.bashrc >> /etc/bash.bashrc
EOF

USER ${MAPPED_USER}
RUN mkdir /home/mapped/ffmpeg
RUN mkdir /home/mapped/ffmpeg/build
WORKDIR /home/mapped/
