#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
output=/home/sean/normal-disk-retained-native.dSunYE
/usr/bin/time -o "$output/launch.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
 sh "$output/run-native.sh" > "$output/launch.log" 2>&1
status=$?
printf '%s\n' "$status" > "$output/outer.status"
exit "$status"
