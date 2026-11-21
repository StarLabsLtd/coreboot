#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Wrap a configured IA32 PEIM in a deterministic FFS for PI file services."""
from pathlib import Path
import struct
import sys
import uuid

image = Path(sys.argv[1]).read_bytes()
if len(image) < 64 or image[:2] != b'MZ':
    raise SystemExit('AMD GOP must be a PE32 image')
pe = struct.unpack_from('<I', image, 60)[0]
if pe > len(image) - 26 or image[pe:pe + 4] != b'PE\0\0' or \
        struct.unpack_from('<H', image, pe + 4)[0] != 0x14c or \
        struct.unpack_from('<H', image, pe + 24)[0] != 0x10b:
    raise SystemExit('AMD GOP must target IA32')
if len(image) > 0xfffffe - 28:
    raise SystemExit('AMD GOP is too large for a normal FFS')
section = (len(image) + 4).to_bytes(3, 'little') + b'\x10' + image
header = bytearray(uuid.UUID('f922dd8d-400c-43f6-951b-93673d73601f').bytes_le +
                   b'\0\0\x06\0' + (24 + len(section)).to_bytes(3, 'little') + b'\0')
header[16] = (-sum(header)) & 255
header[17] = 0xaa
header[23] = 0xf8
Path(sys.argv[2]).write_bytes(header + section)
