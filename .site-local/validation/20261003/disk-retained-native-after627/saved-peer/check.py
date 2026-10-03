# SPDX-License-Identifier: GPL-2.0-only
"""Independent saved-run replay; this does not launch a guest."""
import json
import sys
from pathlib import Path

source = Path('/home/sean/Documents/.cdk2-worktrees/disk-retained-four-epoch-observer-after623')
run = Path('/home/sean/normal-disk-retained-native.dSunYE/run-2')
producer = Path('/home/sean/normal-efi-disk-producers-after625.ZBl2DT')
sys.path.insert(0, str(source / 'tests'))
from system_fmp_disk_retained_native_test import check_saved, check_markers, check_media, digest
sys.path.insert(0, '/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388/util/efi_capsule')
from validate_capsule import parse_fmap_regions

result = json.loads((run / 'result.json').read_text())
if result['failure'] is not None or result['qemu_status'] != 3 or \
        result['budget_seconds'] != 180 or not 0 < result['wall_seconds'] <= 180:
    raise ValueError('actual bounded guest success required')
boots = [run / f'boot-{index}' for index in range(4)]
if result['epochs'] != [path.name for path in boots]:
    raise ValueError('actual four accepted epochs required')
check_saved(boots, (producer / 'disk-a.cap').read_bytes())
check_markers((run / 'serial.log').read_bytes(), result['events'])
initial = (producer / 'initial9/build/coreboot.rom').read_bytes()
target = (producer / 'targetA/build/coreboot.rom').read_bytes()
check_media((run / 'pflash.rom').read_bytes(), initial, target, parse_fmap_regions(initial))
if digest(run / 'pflash.rom') != result['installed_sha256'] or \
        digest(run / 'nvme.raw') != result['disk_sha256'] or \
        result['disk_sha256'] != result['staged_disk_sha256']:
    raise ValueError('actual final media/staged disk digests differ')
if (run / 'retained-disk-a.cap').read_bytes() != (producer / 'disk-a.cap').read_bytes():
    raise ValueError('original on-disk signed capsule identity differs')
before = json.loads((run / 'inputs-before.json').read_text())
after = json.loads((run / 'inputs-after.json').read_text())
if before != after or any(digest(Path(path)) != expected for path, expected in before.items()):
    raise ValueError('actual caller/source/tool/profile assets changed')
print(f'Saved actual four epochs, three guest resets, media/capsule and {len(before)} input bindings: PASS')
