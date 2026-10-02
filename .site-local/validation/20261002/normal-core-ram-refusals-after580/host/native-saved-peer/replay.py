# SPDX-License-Identifier: GPL-2.0-only
"""Read-only replay of saved native refusal outcomes; does not start a VM."""
import json
import sys
from pathlib import Path

source = Path('/home/sean/Documents/.cdk2-worktrees/system-fmp-core-ram-refusals-after575')
sys.path.insert(0, str(source / 'tests'))
import system_fmp_core_ram_native_test as runner

runs = Path('/home/sean/normal-core-ram-refusals.oIoDHH')
fixtures = Path('/home/sean/normal-core-ram-refusal-fixtures.VxQuI9')
initial = Path('/home/sean/system-fmp-provider-session-final.GCQCjf/original-pristine-full-core.rom').read_bytes()
cases = {
    'wrong-signer': 'wrong-signer-a.cap',
    'signed-byte': 'signed-byte-corrupt-a.cap',
    'below-floor': 'signed-below-floor-8.cap',
}
for case, filename in cases.items():
    run = runs / case / 'run-1'
    capsule = fixtures / filename
    result = json.loads((run / 'result.json').read_text())
    runner.check_boots([run / 'boot-0', run / 'boot-1'], capsule.stat().st_size,
                       refusal=case)
    runner.check_refusal_markers((run / 'serial.log').read_bytes(), result['events'])
    runner.check_refusal_media((run / 'pflash.rom').read_bytes(), initial,
                              {'SMMSTORE': (0, 65536)})
    assert (run / 'boot-1/retained-capsule.bin').read_bytes() == capsule.read_bytes()
    assert json.loads((run / 'inputs-before.json').read_text()) == \
        json.loads((run / 'inputs-after.json').read_text())
    assert result['failure'] is None
    assert result['completion'] == 'HOST termination after validated refusal'
    print(case + ': saved strict epochs/status/markers/media/input identity PASS')
print('Saved-outcome replay only: no fresh VM, guest success or OS-continuation claim.')
