# SPDX-License-Identifier: GPL-2.0-only
"""Read-only saved cold outcome replay; no independent guest invocation."""
import hashlib
import json
import struct
import sys
from pathlib import Path

source = Path('/home/sean/Documents/.cdk2-worktrees/capsule-report-local-floor-cold-after607')
sys.path.insert(0, str(source / 'tests'))
sys.path.insert(0, '/home/sean/Documents/.coreboot-worktrees/capsule-signature-refusal-after386/util/efi_capsule')
from capsule_report_cold_native_test import check_cold_boot
from validate_capsule import parse_capsule_details

run = Path('/home/sean/capsule-report-local-floor-cold-retry.Aowjcu/run-1')
warm = Path('/home/sean/normal-local-floor-producer.uE8CdA/below-floor/run-1')
capsule = Path('/home/sean/normal-local-floor-producer.uE8CdA/below-floor-8.cap')
result = json.loads((run / 'result.json').read_text())
assert result['failure'] is None and result['qemu_status'] == 3
assert result['expected_local_floor_refusal'] is True
assert 0 < result['wall_seconds'] <= result['budget_seconds'] == 180
check_cold_boot(run / 'boot-0', parse_capsule_details(capsule.read_bytes())['image_guid'].bytes_le,
                (run / 'serial.log').read_bytes(), result['events'], True)
before = (run / 'report-last-before.bin').read_bytes()
assert len(before) == 94 and before == (run / 'report-last-after.bin').read_bytes()
assert struct.unpack_from('<Q', before, 40)[0] == 0x8000000000000003
assert before[72:] == 'Capsule0000'.encode('utf-16le')
initial, final = (warm / 'pflash.rom').read_bytes(), (run / 'pflash.rom').read_bytes()
assert len(initial) == len(final) == 8388608 and initial[65536:] == final[65536:]
assert hashlib.sha256(final).hexdigest() == result['installed_sha256']
assert hashlib.sha256((run / 'nvme.raw').read_bytes()).hexdigest() == \
    result['disk_sha256'] == result['disk_baseline_sha256']
inputs = json.loads((run / 'inputs-before.json').read_text())
assert inputs == json.loads((run / 'inputs-after.json').read_text())
for name, expected in inputs.items():
    assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == expected, name
print('Saved-only actual cold local floor: strict table/console, exact94, UNSUPPORTED, noRESET, firmware/disk/input hashes PASS')
