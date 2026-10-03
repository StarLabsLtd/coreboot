#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Bounded normal UI oracle: exact existing pixel rules, not acceptance config."""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import uuid

stage = Path(__file__).resolve().parent
source = Path('/home/sean/Documents/.cdk2-worktrees/phase-owned-lvgl-status-after614')
run = stage / 'ui-run'
oracle = source / 'util/qemu/bin/assert-setup-run.py'
text = oracle.read_text()
anchor = '\nserial_path = run / "serial.log"\n'
assert text.count(anchor) == 1
prefix = text.split(anchor)[0]
# Exact original full screenshot, source geometry, Selecting strip,
# outside-pixel restoration, action transcript and completion checks.
# The later acceptance-profile config/manifest contract is intentionally
# not invoked or relabeled as a whole-suite normal-profile pass.
(stage / 'pixel-oracle-source-binding.json').write_text(json.dumps({
    'full_source_sha256': hashlib.sha256(oracle.read_bytes()).hexdigest(),
    'verbatim_prefix_sha256': hashlib.sha256(prefix.encode()).hexdigest(),
    'anchor': anchor, 'scope': 'existing visual/action/completion prefix only'}, indent=2) + '\n')
sys.path.insert(0, str(source / 'util/qemu/bin'))
sys.argv = [str(oracle), str(run), 'hotkey', 'quiet']
exec(compile(prefix, str(oracle), 'exec'), {'__name__': '__main__', '__file__': str(oracle)})
from qmp_cbmem_console import parse_table, validate_saved_console
body = validate_saved_console(run)
metadata = json.loads((run / 'cbmem-observer.json').read_text())
records = parse_table((run / f'cbmem-table-{len(metadata["tables"]) - 1}.bin').read_bytes())
identities = [record[8:] for tag, record in records if tag == 0x45]
assert identities == [uuid.UUID('00112233-4455-6677-8899-aabbccddeeff').bytes_le +
                      struct.pack('<III', 0x001a0009, 0x001a0009, 8388608)]
assert not any(tag == 0x46 for tag, _ in records)
positions = []
for phase, event, status in (
        (b'INPUT_UI', b'begin', b'NOT_STARTED \\(0x8000000000000013\\)'),
        (b'INPUT_UI', b'complete', b'SUCCESS \\(0x0000000000000000\\)')):
    prefix = b'CDK2 | ' + phase + b' | ' + event + b' | '
    lines = [line for line in body.splitlines() if line.startswith(prefix[:-3])]
    assert len(lines) == 1 and re.fullmatch(re.escape(prefix) + status + rb' \| [0-9]+ us', lines[0])
    positions.append(body.find(lines[0]))
assert positions == sorted(set(positions))
assert b'SystemFmp CHECK transport' not in body and b'SystemFmp SET transport' not in body
result = json.loads((run / 'result.json').read_text())
assert result['failure'] is None and result['observer_status'] == result['qemu_status'] == 0
assert json.loads((run / 'inputs-before.json').read_text()) == json.loads((run / 'inputs-after.json').read_text())
print('Normal623 actual INPUT_UI/status pixels/F2/navigation/restoration + existing runtime marker: PASS')
print('Not an unchanged acceptance-profile whole-suite, BootToFwUI request, capsule update, or hardware claim')
