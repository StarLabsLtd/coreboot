#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
output=/home/sean/normal-disk-retained-native.dSunYE
/usr/bin/time -o "$output/launch-2.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
 sh "$output/run-native-2.sh" > "$output/launch-2.log" 2>&1
status=$?
printf '%s\n' "$status" > "$output/outer-2.status"
exit "$status"
