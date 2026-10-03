# SPDX-License-Identifier: GPL-2.0-only
"""Read-only verification of the independently owned native continuation."""
import hashlib
import importlib.util
import json
import sys
from pathlib import Path

source = Path('/home/sean/Documents/.cdk2-worktrees/certified-refusal-continuation-peer-after595.3Qdgg0')
root = Path('/home/sean/normal-refusal-consumed-producer.VPNIdB')
run = Path('/home/sean/certified-refusal-continuation-peer.Js7GPg/wrong-signer/run-1')
sys.path.insert(0, str(source / 'tests'))
spec = importlib.util.spec_from_file_location('runner', source / 'tests/system_fmp_core_ram_native_test.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
result = json.loads((run / 'result.json').read_text())
assert result['failure'] is None and result['qemu_status'] == 3
assert result['refusal_observed'] and result['completion'] == 'guest continuation after certified refusal'
assert result['budget_seconds'] == 180 and result['wall_seconds'] <= 180
before = json.loads((run / 'inputs-before.json').read_text())
assert before == json.loads((run / 'inputs-after.json').read_text())
for name, expected in before.items():
    assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == expected, name
capsule = root / 'wrong-signer-a.cap'
runner.check_boots(sorted(run.glob('boot-*')), capsule.stat().st_size, refusal='wrong-signer', certified=True)
runner.check_refusal_markers((run / 'serial.log').read_bytes(), result['events'], True)
assert (run / 'boot-1/retained-capsule.bin').read_bytes() == capsule.read_bytes()
runner.check_refusal_media((run / 'pflash.rom').read_bytes(), (root / 'initial9/build/coreboot.rom').read_bytes(), {'SMMSTORE': (0, 65536)})
assert runner.digest(run / 'pflash.rom') == result['installed_sha256']
assert runner.digest(run / 'nvme.raw') == result['disk_sha256']
core = Path('/home/sean/capsule-linear-completion-normal.fw344b/native/cdk2-coreboot-image.elf')
assert runner.loads(run / 'initial-core.elf') == runner.loads(core)
assert runner.loads(run / 'target-core.elf') == runner.loads(core)
print('Fresh wrong-signer saved readback PASS: strict two FW9 epochs, one warm RESET, actual certified RAM complete before PCI, ordinary MAIN request absence/zero state, retained candidate, input/current hashes, Core loaded bytes, outside-store and final disk hash.')
print('Disk pre/post equality is source-executed by the native runner; this saved check verifies its recorded final hash, not a reconstructed initial disk.')
