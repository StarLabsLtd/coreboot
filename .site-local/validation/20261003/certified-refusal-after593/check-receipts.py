#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read-only checks of this finite source/build receipt packet."""
import hashlib
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parent
REPO = Path('/home/sean/Documents/.cdk2-worktrees/capsule-certified-refusal-after588')
HEAD = '3b022120d08dca81b8dfec0e3acf344679fe602b'
EXPECTED = 'd3863ab002696c2622199262d96ff68cb38b9f01c62fe0c537799775b0e2dd76'
paths = (ROOT / 'source/paths.txt').read_text().splitlines()
assert len(paths) == 24 and len(set(paths)) == 24
with tarfile.open(ROOT / 'source/signed-593-source.tar.gz') as archive:
    members = [item for item in archive.getmembers() if item.isfile()]
    assert sorted(item.name for item in members) == sorted(paths)
    for member in members:
        content = archive.extractfile(member).read()
        signed = subprocess.check_output(['git', '-C', str(REPO), 'show',
                                          f'{HEAD}:{member.name}'])
        assert content == signed == (REPO / member.name).read_bytes()
for name in ['source/current-24.sha256', 'peer/source-final-before.sha256',
             'peer/certified-inputs-before.sha256', 'peer/config-before.sha256']:
    subprocess.run(['sha256sum', '-c', str(ROOT / name)], cwd=REPO, check=True)
elves = [Path('/home/sean/capsule-certified-refusal-normal.BVJbj2/native/cdk2-coreboot-image.elf'),
         Path('/home/sean/capsule-certified-refusal-peer.PxZI5K/native/cdk2-coreboot-image.elf')]
contents = [item.read_bytes() for item in elves]
assert contents[0] == contents[1]
assert all(hashlib.sha256(item).hexdigest() == EXPECTED for item in contents)
print('24 signed source bodies, final manifests and two actual Core ELF files: PASS')
