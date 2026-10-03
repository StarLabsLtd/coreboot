# SPDX-License-Identifier: GPL-2.0-only
"""Validate finite copied cold receipts; no guest launch or external media write."""
import json
import re
import struct
import sys
import uuid
from pathlib import Path

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parent
sys.path.insert(0, str(root / 'observer'))
from capsule_report_cold_native_test import check_cold_boot

for name in ('author', 'independent'):
    case = root / name
    result = json.loads((case / 'result.json').read_text())
    assert result['failure'] is None and result['qemu_status'] == 3
    assert result['budget_seconds'] == 180 and 0 < result['wall_seconds'] <= 180
    assert result['completion'] == 'ordinary cold MAIN persisted report and normal locks'
    assert result['disk_sha256'] == result['disk_baseline_sha256']
    assert (case / 'inputs-before.json').read_bytes() == (case / 'inputs-after.json').read_bytes()
    config = (case / 'codec-1/producer.config').read_text()
    guids = re.findall(r'^CONFIG_DRIVERS_EFI_MAIN_FW_GUID="([^"]+)"$', config, re.MULTILINE)
    assert len(guids) == 1
    image_guid = uuid.UUID(guids[0]).bytes_le
    check_cold_boot(case / 'boot-0', image_guid, (case / 'serial.log').read_bytes(), result['events'])
    records = (case / 'report-last-before.bin').read_bytes()
    assert len(records) == 94 and records == (case / 'report-last-after.bin').read_bytes()
    assert struct.unpack_from('<II', records) == (72, 0)
    assert records[8:24] == (case / 'prior-capsule-guid.bin').read_bytes()
    assert struct.unpack_from('<Q', records, 40)[0] == 0x8000000000000007
    assert records[48:51] == bytes((1, 0, 0)) and records[51] != 0
    assert records[52:68] == image_guid and records[68:72] == bytes(4)
    assert records[72:] == 'Capsule0000'.encode('utf-16le')
    print(f'{name}: saved real FW9/no RESET/MAIN, fixed report fields, exact94 persistence '
          'and unchanged input maps PASS')
print('Copied saved-receipt checker only; actual native statuses come from sessions79313/49252.')
