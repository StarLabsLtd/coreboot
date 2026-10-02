#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read-only restore receipts and external exact-media comparisons."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
author = Path('/home/sean/system-fmp-core-ram-restore-final.hqVPFk')
peer = Path('/home/sean/system-fmp-core-ram-restore-peer.uqD9Wr')
expected = dict(attempt=0x001a000b, target_running=0x001a0009,
                prior_running=0x001a000a, prior_history=0x001a000a)
for label, original, run in (('author-restore', author, 'run-2'),
                             ('peer-restore', peer, 'run-1'),
                             ('failed-reset-console-motion', author, 'run-1')):
    directory = root / label
    before = json.loads((directory / 'inputs-before.json').read_text())
    after = json.loads((directory / 'inputs-after.json').read_text())
    assert before == after
    assert json.loads((directory / 'expected-versions.json').read_text()) == expected
    result = json.loads((directory / 'result.json').read_text())
    initial_path = next(path for path, value in before.items() if
                        value == '136d2259c24eead60340e9e4edd009bffb7e2de5513836240f3b150278f2a6a3')
    initial = Path(initial_path).read_bytes()
    media = (original / run / 'pflash.rom').read_bytes()
    target = (author / 'original-pristine-full-core.rom').read_bytes()
    assert media[65536:73728] == initial[65536:73728]
    if label != 'failed-reset-console-motion':
        assert result['failure'] is None and result['qemu_status'] == 3
        assert result['wall_seconds'] <= 180
        assert sum(event.get('event') == 'RESET' for event in result['events']) == 2
        assert media[73728:] == target[73728:]
        retained = (original / run / 'boot-1/retained-capsule.bin').read_bytes()
        assert retained == (author / 'signed-restore-capsule.bin').read_bytes()
    else:
        assert result['failure'] == 'CBMEM console changed through every bounded snapshot'
        assert media[73728:] == initial[73728:]
    print(label, len(before), 'original hashes unchanged; exact COREBOOT/probe readback PASS')
    print(label, 'final-media', hashlib.sha256(media).hexdigest())
