#!/bin/bash

# This script is used to promotes a build to xsjzohaibm44x so others can use it

ARTIFACTS=(
  "ffmpeg" 
  "ffmpeg_g" 
  "ffprobe" 
  "ffprobe_g" 
)

for f in ${ARTIFACTS[@]}; do
  scp staticx/$f zohaibm@xsjzohaibm44x:/group/ngcodec/ma35_firmware/ffmpeg/bin/.
  scp staticx/$f zohaibm@xcozohaibm41x:/proj/video_qa/MA35_QA/tools/ma35_firmware_tools/ffmpeg/bin/.
done
ssh zohaibm@xsjzohaibm44x "chmod a+rx /proj/xsjhdstaff6/zohaibm/local/ffmpeg/bin/*"
ssh zohaibm@xcozohaibm41x "chmod a+rx /proj/video_qa/MA35_QA/tools/ma35_firmware_tools/ffmpeg/bin/*"


