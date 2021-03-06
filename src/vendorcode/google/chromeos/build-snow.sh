#!/bin/sh

SCRIPT="$0"
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

OUTPUT="build/coreboot.rom"
TMP_DIFF="$SCRIPT_DIR/.image-diff.bin"
FLASHROM="/usr/local/sbin/flashrom"

BL1_NAME="E5250.nbl1.bin"
BL1_PATH="3rdparty/blobs/cpu/samsung/exynos5250/"
BL1_URL="http://commondatastorage.googleapis.com/chromeos-localmirror/distfiles/exynos-pre-boot-0.0.2-r8.tbz2"

die() {
  echo "$*" >&2
  exit 1
}

create_diff_192k() {
  local image_file="$1"
  local diff_file="$2"
  cp -f "$image_file" "$diff_file"
  dd if=/dev/zero of=$diff_file bs=1 count=$((192*1024)) conv=notrunc
}

fast_flash_image() {
  local image_file="$1"
  local diff_file="$2"
  dut-control spi2_buf_en:on spi2_buf_on_flex_en:on spi2_vref:pp1800
  sudo ${FLASHROM} -p ft2232_spi:type=servo-v2,port=a -w "$image_file" -V \
    --noverify --flash-contents "$diff_file"
  dut-control spi2_buf_en:off spi2_buf_on_flex_en:off spi2_vref:off
}

get_bl1() {
  wget "${BL1_URL}" -O /tmp/bl1.tbz2
  tar jxvf /tmp/bl1.tbz2
  mkdir -p "${BL1_PATH}"
  mv "exynos-pre-boot/firmware/${BL1_NAME}" "${BL1_PATH}"
  rm -rf exynos-pre-boot
  if [ ! -e "${BL1_PATH}/${BL1_NAME}" ]; then
    echo "Error getting BL1"
    exit 1
  fi
}

is_servod_ready() {
  ps -C servod >/dev/null 2>&1
}

main() {
  if [ ! -e "$bl1" ]; then
    get_bl1
  fi

  make
  create_diff_192k "$OUTPUT" "$TMP_DIFF"
  echo "OK: Generated image (with BL1) in $OUTPUT"
  if is_servod_ready; then
    echo "servod detected - flashing into device."
    fast_flash_image "$OUTPUT" "$TMP_DIFF"
    echo "OK: Generated and flashed 128k of image into device via servo."
  else
    echo "(servod is not running, flashing into device is skipped)"
  fi
}

set -e
main "$@"
