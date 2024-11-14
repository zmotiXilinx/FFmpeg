#!/bin/bash

# This script is used to promotes a build to xsjzohaibm44x so others can use it

ARTIFACTS=(
  "ffmpeg" 
  "ffmpeg_g" 
  "ffprobe" 
  "ffprobe_g" 
)

for f in ${ARTIFACTS[@]}; do
  scp staticx/$f zohaibm@xsjzohaibm44x:/proj/xsjhdstaff6/zohaibm/local/ffmpeg/bin/.
done