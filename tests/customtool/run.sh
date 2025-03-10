#!/bin/bash

# this script runs tests related to custom functionality added to ffmpeg:
#    inject_cc - inject closed captions into video ES
#    extract_cc - extract closed captions from video ES
#    inject_hdr10plus - inject HDR10+ metadata into video ES
#    extract_hdr10plus - extract HDR10+ metadata from video ES

script_dir=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
ffmpeg_bin="$(realpath $script_dir/../../ffmpeg)"
kimono_yuv="/mnt/c/Users/zohaibm/group/ngcodec/video_clips/720x480/Kimono1_720x480_24.yuv"
kimono_srt="$(realpath $script_dir/ref/kimono.srt)"
out_dir="$(realpath $script_dir/out)"
ref_dir="$(realpath $script_dir/ref)"

compare_md5 () {
    computed_md5=$1
    reference_md5=$2
    if [[ "$computed_md5" == "$reference_md5" ]]; then
        return 1
    else
        return 0
    fi
}

get_ffmpeg_codec () {
  codec=$1
  case $codec in
      "h264")
          echo "libx264"
          ;;
      "hevc")
          echo "libx265"
          ;;
      "av1")
          echo "libsvtav1"
          ;;
      *)
          echo "Invalid codec: $codec"
          exit 1
          ;;
  esac
}

# Function to compare two files
compare_files() {
  # Set the function's input parameters
  file1_path="$1"
  file2_path="$2"

  # Compare the files
  diff_output=$(diff "$file1_path" "$file2_path")

  # Evaluate the comparison result
  if [ -n "$diff_output" ]; then
    echo "Files are different:"
    echo "$diff_output"
    return 1
  else
    return 0
  fi
}

for codec in "h264" "hevc" "av1"; do
  ffmpeg_codec=$(get_ffmpeg_codec $codec)
  out_bitstream="$out_dir/kimono_480p30_cc.$codec.mp4"
  $ffmpeg_bin -y  -r 30 -s:v 720x480 -pix_fmt yuv420p -i $kimono_yuv \
      -vf inject_cc=filename=$kimono_srt -c:v $ffmpeg_codec -a53cc true -crf 31 \
      $out_bitstream > $out_bitstream.log 2>&1
  out_srt="$out_dir/kimono_480p30_cc.$codec.srt"
  $ffmpeg_bin -y -i $out_bitstream -vf extract_cc=filename=$out_srt \
      -f null - > $out_srt.log 2>&1
  compare_files "$ref_dir/kimono.out.ref.srt" "$out_srt"
  # Check the function's return value
  if [ $? -eq 0 ]; then
    echo "Test passed: inject/extract_cc codec=$codec"
  else
    echo "Test failed: inject/extract_cc codec=$codec"
  fi
done


for codec in "h264" "hevc" "av1"; do
  ffmpeg_codec=$(get_ffmpeg_codec $codec)
  out_bitstream="$out_dir/kimono_480p30_hdr10plus.$codec.mp4"
  hdr10plus_data=$ref_dir/kimono_480p60_hdr10plus.json
  $ffmpeg_bin -y  -r 30 -s:v 720x480 -pix_fmt yuv420p -i $kimono_yuv -pix_fmt yuv420p10le \
      -vf inject_hdr10plus=filename=$hdr10plus_data -c:v $ffmpeg_codec -hdr10plus true -crf 31 \
      $out_bitstream > $out_bitstream.log 2>&1
  out_hdr10plus="$out_dir/kimono_480p60_hdr10plus.$codec.json"
  $ffmpeg_bin -y -i $out_bitstream -vf extract_hdr10plus=filename=$out_hdr10plus \
      -f null - > $out_hdr10plus.log 2>&1
  compare_files "$hdr10plus_data" "$out_hdr10plus"
  # Check the function's return value
  if [ $? -eq 0 ]; then
    echo "Test passed: inject/extract_hdr10plus codec=$codec"
  else
    echo "Test failed: inject/extract_hdr10plus codec=$codec"
  fi
done
