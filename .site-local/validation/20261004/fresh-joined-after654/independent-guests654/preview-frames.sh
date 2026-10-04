#!/usr/bin/env bash
# Lossless viewing derivatives only. No guest image or receipt is edited.
set -euo pipefail
review=/home/sean/fresh-normal-guest-independent-review654.o7hehD
guests=/home/sean/fresh-normal-joined-guest-after654.Vr3XYD
case ${1:-} in
  ui-cancel) run=$guests/ui-cancel/lifecycle/ui-cancel
    labels=(status-original form-initial form-navigated form-control-edited form-dependent-focused firmware-restored) ;;
  linux-reset) run=$guests/linux-reset/lifecycle/same-vm
    labels=(status-original form-initial form-navigated firmware-restored) ;;
  *) exit 2 ;;
esac
output=$review/$1-previews
mkdir "$output"
sha256sum "$0" /usr/bin/ffmpeg > "$output/preview-tools.sha256"
for label in "${labels[@]}"; do
  original=$run/setup-$label.ppm
  preview=$output/$label.png
  sha256sum "$original" > "$output/$label-original-before.sha256"
  /usr/bin/ffmpeg -nostdin -v error -n -i "$original" -frames:v 1 \
    -threads 1 -pix_fmt rgb24 -update 1 "$preview"
  original_rgb=$(tail -c 1440000 "$original" | sha256sum | cut -d' ' -f1)
  preview_rgb=$(/usr/bin/ffmpeg -nostdin -v error -i "$preview" -frames:v 1 \
    -threads 1 -f rawvideo -pix_fmt rgb24 - | sha256sum | cut -d' ' -f1)
  printf '%s\t%s\n' "$original_rgb" "$preview_rgb" > "$output/$label-rgb-sha256.tsv"
  test "$original_rgb" = "$preview_rgb"
  sha256sum -c "$output/$label-original-before.sha256" > "$output/$label-original-after-check.log"
done
sha256sum -c "$output/preview-tools.sha256" > "$output/preview-tools-after-check.log"
printf '0\n' > "$output/preview.status"
