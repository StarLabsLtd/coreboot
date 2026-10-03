# SPDX-License-Identifier: GPL-2.0-only
"""Read-only checks over archived receipts; no VM or authority callback."""
import hashlib
import json
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WT = Path('/home/sean/Documents/.cdk2-worktrees/disk-capsule-denial-native-after612')
COMMITS = {'original': 'abae9576b3691d713fa31b424b5e0c7d0b925ee6',
           'corrected': 'f76c410602',
           'ready616': 'c702e10fcc104e7059211e4342704f06f7c11b78'}
for label, commit in COMMITS.items():
    with tarfile.open(ROOT / f'source-{label}.tar') as archive:
        members = [member for member in archive.getmembers() if member.isfile()]
        assert len(members) == 2
        for member in members:
            body = archive.extractfile(member).read()
            actual = subprocess.check_output(['git', '-C', str(WT), 'show',
                                               f'{commit}:{member.name}'])
            assert body == actual
            if label == 'ready616':
                assert body == (WT / member.name).read_bytes()
for label in ('run-1', 'run-2'):
    before = json.loads((ROOT / label / 'inputs-before.json').read_text())
    after = json.loads((ROOT / label / 'inputs-after.json').read_text())
    assert len(before) == 24981 and before == after
first = json.loads((ROOT / 'run-1/result.json').read_text())
final = json.loads((ROOT / 'run-2/result.json').read_text())
assert first['failure'] is not None and first['observation'] is None
assert first['qemu_seconds'] >= first['deadline_seconds'] == 180
assert final['failure'] is None and final['deadline_seconds'] == 180
assert final['qemu_seconds'] < 180 and final['observation']['transport'] == 8
assert final['observation']['transport_name'] == 'BAD_RESPONSE'
assert final['termination'] == 'HOST after bounded denial, not guest boot success'
sys.path.insert(0, str(WT / 'util/qemu/bin'))
from qmp_cbmem_console import validate_saved_console
sys.path.insert(0, str(WT / 'tests'))
from system_fmp_disk_denial_native_test import observation
body = validate_saved_console(ROOT / 'run-2')
metadata = json.loads((ROOT / 'run-2/cbmem-observer.json').read_text())
tables = [(ROOT / f'run-2/cbmem-table-{index}.bin').read_bytes()
          for index in range(len(metadata['tables']))]
assert observation(body, tables) == final['observation']
external = json.loads((ROOT / 'external-artifact-digests.json').read_text())
for name, expected in external.items():
    value = hashlib.sha256()
    with Path(name).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    assert value.hexdigest() == expected
print('Archived source/raw maps + saved actual run2 + external artifact hashes: PASS')
if sys.argv[1:] == ['--write-manifest']:
    paths = sorted(path for path in ROOT.rglob('*')
                   if path.is_file() and path.name != 'files.sha256')
    lines = [f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(ROOT)}\n'
             for path in paths]
    (ROOT / 'files.sha256').write_text(''.join(lines))
    print(f'Generated manifest over {len(paths)} archived files')
