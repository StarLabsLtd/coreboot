#!/bin/bash
rm -r build
meson build -D processor=apollolake -D stitch_dir=/home/sean/Documents/APL -D bootguard=true
sudo flashrom -p ch341a_spi -w build/coreboot.rom -n
