#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
import hashlib
import json
from pathlib import Path

from PIL import Image

root = Path(__file__).resolve().parent / 'default-zero-queued-hotkey-7'
receipts = []
for name in ('setup-form-initial', 'setup-form-navigated', 'setup-firmware-restored'):
    source = root / (name + '.ppm')
    target = root / (name + '.png')
    if target.exists():
        raise SystemExit(f'refusing to overwrite {target}')
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    with Image.open(source) as original:
        original.load()
        original.save(target, format='PNG')
        with Image.open(target) as converted:
            converted.load()
            if original.mode != converted.mode or original.size != converted.size or \
                    original.tobytes() != converted.tobytes():
                raise SystemExit('lossless pixel comparison failed')
    if hashlib.sha256(source.read_bytes()).hexdigest() != before:
        raise SystemExit('original screenshot changed')
    receipts.append({'original': source.name, 'original_sha256': before,
                     'png': target.name,
                     'png_sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
                     'lossless_pixels_equal': True})
print(json.dumps(receipts, indent=2))
