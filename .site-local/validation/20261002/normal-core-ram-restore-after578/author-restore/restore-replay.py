# SPDX-License-Identifier: GPL-2.0-only
"""Read-only replay of saved actual restore, not an independent native boot."""

import hashlib
import importlib.util
import json
from pathlib import Path

source = Path('/home/sean/Documents/.cdk2-worktrees/system-fmp-core-ram-restore-after573')
artifact = Path('/home/sean/system-fmp-core-ram-restore-final.hqVPFk')
run = artifact / 'run-2'
spec = importlib.util.spec_from_file_location('actual_runner', source / 'tests/system_fmp_core_ram_native_test.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
result = json.loads((run / 'result.json').read_text())
assert result['failure'] is None and result['qemu_status'] == 3
assert result['budget_seconds'] == 180 and result['wall_seconds'] < 180
resets = [event for event in result['events'] if event['event'] == 'RESET']
assert len(resets) == 2 and all(event['data']['guest'] for event in resets)
capsule = (artifact / 'signed-restore-capsule.bin').read_bytes()
assert (run / 'boot-1/retained-capsule.bin').read_bytes() == capsule
runner.check_boots([run / f'boot-{index}' for index in range(3)], len(capsule),
                   0x001a000a, 0x001a0009)
assert runner.loads(run / 'initial-core.elf') == runner.loads(artifact / 'native/cdk2-coreboot-image.elf')
assert runner.loads(run / 'target-core.elf') == runner.loads(artifact / 'native/cdk2-coreboot-image.elf')
uart = (run / 'serial.log').read_bytes()
for marker, count in [('MAIN_ENTRY', 2), ('SETUP1_SECURE0', 2), ('REAL_SYSTEM_FMP_INFO', 2),
                      ('ACTUAL_INITIAL_ZERO_STATE', 0), ('ACTUAL_PRIOR_SUCCESS_STATE', 1),
                      ('QUERY_STRUCTURAL_ONLY', 1), ('UPDATE_CAPSULE_RESET', 1)]:
    assert uart.count(('CDK2_CORE_RAM_STAGE_' + marker).encode()) == count
assert uart.count(b'CDK2_CORE_RAM_STAGE_FAIL') == 0
assert uart.count(b'CDK2_CORE_RAM_ACTUAL_NORMAL_CORE_COLD_RUNNING_AND_HISTORY_PASS') == 1
installed = (run / 'pflash.rom').read_bytes()
target = (artifact / 'original-pristine-full-core.rom').read_bytes()
initial = (artifact / 'successful-installed-input.rom').read_bytes()
assert len(installed) == len(target) == len(initial) == 8388608
assert installed[73728:] == target[73728:]
assert installed[65536:73728] == initial[65536:73728]
assert hashlib.sha256(installed).hexdigest() == result['installed_sha256']
assert hashlib.sha256((run / 'nvme.raw').read_bytes()).hexdigest() == result['disk_sha256']
before = json.loads((run / 'inputs-before.json').read_text())
after = json.loads((run / 'inputs-after.json').read_text())
assert before == after
for path, expected in before.items():
    assert hashlib.sha256(Path(path).read_bytes()).hexdigest() == expected, path
print('Saved actual restore epochs a/a/9, two guest resets, exact signed b bytes and full Core loads: PASS')
print('Ordinary MAIN prior a/history a and cold running9/history b; exact original COREBOOT/probe/disk binding: PASS')
print('CLOSE-before-success reset is source-flow only; no independent fresh boot or hardware claim')
