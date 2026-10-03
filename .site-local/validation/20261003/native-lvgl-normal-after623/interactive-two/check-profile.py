#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Require the sole resolved normal-profile delta to be timeout zero to two."""
from pathlib import Path
import hashlib
import json

stage = Path(__file__).resolve().parent
baseline = Path('/home/sean/native-lvgl-normal-after623.zLwvcS/resolved.config')
candidate = stage / 'resolved.config'
before = baseline.read_bytes()
after = candidate.read_bytes()
old = b'CONFIG_CDK2_BOOT_TIMEOUT=0\n'
new = b'CONFIG_CDK2_BOOT_TIMEOUT=2\n'
assert before.count(old) == 1
assert after == before.replace(old, new)
header = (stage / 'include/cdk2/config.h').read_text()
for name in ('PROTECTED_VARIABLE_RUNTIME', 'NATIVE_SYSTEM_FMP',
             'LVGL_RENDERER', 'LINEAR_SETUP_HOTKEY'):
    assert f'#define CONFIG_CDK2_{name} 1\n' in header
for name in ('NATIVE_QEMU_TEST_FMP', 'QEMU_ACCEPTANCE_PROFILE', 'BUILD_DEBUG'):
    assert f'#define CONFIG_CDK2_{name} 0\n' in header
assert '#define CONFIG_CDK2_BOOT_TIMEOUT 2\n' in header
print(json.dumps({'baseline': str(baseline), 'baseline_sha256':
                  hashlib.sha256(before).hexdigest(), 'candidate': str(candidate),
                  'candidate_sha256': hashlib.sha256(after).hexdigest(),
                  'only_delta': 'CONFIG_CDK2_BOOT_TIMEOUT=0 -> 2',
                  'production_default_changed': False}, indent=2))
