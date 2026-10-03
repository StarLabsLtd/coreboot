#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Verify ffmpeg format-only PNG derivatives of preserved PPM captures."""
from pathlib import Path
import hashlib
import json
from PIL import Image

root = Path(__file__).resolve().parent / 'ui-run'
records = []
for original in sorted(root.glob('setup-*.ppm')):
    derivative = original.with_suffix('.png')
    with Image.open(original) as before, Image.open(derivative) as after:
        assert before.size == after.size
        assert before.convert('RGB').tobytes() == after.convert('RGB').tobytes()
        records.append({
            'original': str(original),
            'original_sha256': hashlib.sha256(original.read_bytes()).hexdigest(),
            'derivative': str(derivative),
            'derivative_sha256': hashlib.sha256(derivative.read_bytes()).hexdigest(),
            'size': before.size,
            'decoded_rgb_equal': True,
        })
assert len(records) == 4
print(json.dumps({'conversion': 'ffmpeg -frames:v 1; no pixel edits',
                  'records': records}, indent=2))
