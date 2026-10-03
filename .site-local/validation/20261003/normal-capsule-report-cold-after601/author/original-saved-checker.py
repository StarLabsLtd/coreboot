# SPDX-License-Identifier: GPL-2.0-only
"""Read-only saved cold validation; this does not launch another guest."""
import hashlib
import json
import sys
from pathlib import Path

source = Path('/home/sean/Documents/.cdk2-worktrees/capsule-report-cold-persistence-after598')
sys.path.insert(0, str(source / 'tests'))
from capsule_report_cold_native_test import check_cold_boot

run = Path('/home/sean/capsule-report-cold-final.E64Ju6/run-1')
warm = Path('/home/sean/normal-capsule-report-corrected-producer.xexfMA/wrong-signer/run-1')
capsule = Path('/home/sean/normal-capsule-report-corrected-producer.xexfMA/wrong-signer-a.cap')
tools = Path('/home/sean/Documents/.coreboot-worktrees/capsule-signature-refusal-after386/util/efi_capsule')
sys.path.insert(0, str(tools))
from validate_capsule import parse_capsule_details

before = json.loads((run / 'inputs-before.json').read_text())
assert before == json.loads((run / 'inputs-after.json').read_text())
for path, expected in before.items():
    assert hashlib.sha256(Path(path).read_bytes()).hexdigest() == expected, path
result = json.loads((run / 'result.json').read_text())
assert result['failure'] is None and result['qemu_status'] == 3
assert result['budget_seconds'] == 180 and 0 <= result['wall_seconds'] <= 180
assert not any(event.get('event') == 'RESET' for event in result['events'])
guid = parse_capsule_details(capsule.read_bytes())['image_guid'].bytes_le
check_cold_boot(run / 'boot-0', guid, (run / 'serial.log').read_bytes(), result['events'])
report_before = (run / 'report-last-before.bin').read_bytes()
assert len(report_before) == 94
assert report_before == (run / 'report-last-after.bin').read_bytes()
media = (run / 'pflash.rom').read_bytes()
assert len(media) == 8388608
assert media[65536:] == (warm / 'pflash.rom').read_bytes()[65536:]
assert hashlib.sha256(media).hexdigest() == result['installed_sha256']
assert hashlib.sha256((run / 'nvme.raw').read_bytes()).hexdigest() == result['disk_sha256']
assert result['disk_sha256'] == result['disk_baseline_sha256']
print(f'Saved cold receipt: {len(before)} immutable inputs, one actual FW9 epoch/no RESET, '
      '94 persisted bytes, staged disk and firmware outside SMMSTORE: PASS')
print('Read-only saved replay only; fresh native status is sourced from actual session79313.')
