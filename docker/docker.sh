#!/bin/bash

if [ ! -f .bash_histor ]; then
    touch .bash_history
fi

docker run -it --rm \
  --volume ${HOME}/ffmpeg:/home/mapped/ffmpeg:rw \
  -w /home/mapped/ffmpeg \
  ma35_ffmpeg:latest /bin/bash -l
