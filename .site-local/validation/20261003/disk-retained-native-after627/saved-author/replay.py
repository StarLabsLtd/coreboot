#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read-only saved receipt replay; no QEMU or variable/media writes."""
import json
from pathlib import Path
import sys

source = Path('/home/sean/Documents/.cdk2-worktrees/disk-retained-four-epoch-observer-after623')
run = Path('/home/sean/normal-disk-retained-native.dSunYE/run-2')
producer = Path('/home/sean/normal-efi-disk-producers-after625.ZBl2DT')
core = Path('/home/sean/normal-efi-disk-core-final.TJMaS8/native/cdk2-coreboot-image.elf')
sys.path.insert(0, str(source / 'tests'))
import system_fmp_disk_retained_native_test as observer
sys.path.insert(0, str(source.parent.parent / '.coreboot-worktrees' / 'capsule-disk-delivery-capability-after388' / 'util/efi_capsule'))
from validate_capsule import parse_fmap_regions

result = json.loads((run / 'result.json').read_text())
if result['failure'] is not None or result['qemu_status'] != 3 or \
        result['budget_seconds'] != 180 or not 0 < result['wall_seconds'] <= 180:
    raise ValueError('actual bounded guest success result required')
capsule = (producer / 'disk-a.cap').read_bytes()
observer.check_markers((run / 'serial.log').read_bytes(), result['events'])
observer.check_saved([run / name for name in result['epochs']], capsule)
initial = (producer / 'initial9/build/coreboot.rom').read_bytes()
target = (producer / 'targetA/build/coreboot.rom').read_bytes()
observer.check_media((run / 'pflash.rom').read_bytes(), initial, target, parse_fmap_regions(initial))
if (run / 'retained-disk-a.cap').read_bytes() != capsule:
    raise ValueError('retained disk capsule identity')
if observer.digest(run / 'pflash.rom') != result['installed_sha256'] or \
        observer.digest(run / 'nvme.raw') != result['disk_sha256'] or \
        result['disk_sha256'] != result['staged_disk_sha256']:
    raise ValueError('actual media and staged disk output hashes')
for label in ('initial', 'target'):
    if observer.loads(run / f'{label}-core.elf') != observer.loads(core):
        raise ValueError('actual packaged full Core loaded bytes')
    packed = observer.config_assignments((run / f'{label}-producer.config').read_text().splitlines())
    config_dir = 'initial9' if label == 'initial' else 'targetA'
    expected = observer.config_assignments((producer / config_dir / 'full.config').read_text().splitlines())
    if packed != expected:
        raise ValueError('actual packaged producer configuration values')
before = json.loads((run / 'inputs-before.json').read_text())
after = json.loads((run / 'inputs-after.json').read_text())
if before != after:
    raise ValueError('before/after immutable input maps')
for name, expected in before.items():
    if observer.digest(Path(name)) != expected:
        raise ValueError(f'current immutable input: {name}')
print(f'Saved-only four-epoch receipt replay PASS: {len(before)} before/after/current input hashes')
print(f"Actual guest3/QEMU {result['wall_seconds']:.9f} seconds; no new VM")
