#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Finite PUBLIC receipt integrity checks; no VM/build/media writes."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--originals', action='store_true')
parser.add_argument('--git', action='store_true')
args = parser.parse_args()


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def safe_path(value):
    path = PurePosixPath(value)
    require(not path.is_absolute() and '..' not in path.parts and str(path) == value,
            'unsafe or noncanonical packet path: ' + value)
    return value


def sha(data):
    return hashlib.sha256(data).hexdigest()


manifest = {}
for line in (root / 'files.sha256').read_text().splitlines():
    match = re.fullmatch(r'([0-9a-f]{64})  \./(.+)', line)
    require(match is not None, 'manifest syntax')
    expected, name = match.groups()
    safe_path(name)
    require(name not in manifest and name != 'files.sha256', 'manifest duplicate/self record')
    manifest[name] = expected
actual = {path.relative_to(root).as_posix() for path in root.rglob('*') if path.is_file()
          and path.name != 'files.sha256'}
require(actual == set(manifest), 'complete finite file manifest closure')
for name, expected in manifest.items():
    require(not (root / name).is_symlink() and sha((root / name).read_bytes()) == expected,
            'file SHA: ' + name)

copied = set()
for line in (root / 'original-copies.tsv').read_text().splitlines():
    fields = line.split('\t')
    require(len(fields) == 3, 'original copy record shape')
    name, original, expected = fields
    safe_path(name)
    require(name not in copied and name in manifest and re.fullmatch(r'[0-9a-f]{64}', expected),
            'original copy duplicate/hash/path')
    copied.add(name)
    require(manifest[name] == expected, 'copied SHA agrees with finite manifest')
    if args.originals:
        require(Path(original).is_absolute() and Path(original).is_file(), 'missing original: ' + original)
        require((root / name).read_bytes() == Path(original).read_bytes(), 'exact original copy: ' + name)

identities = {}
archive_commits = {}
for line in (root / 'source-identities.tsv').read_text().splitlines():
    fields = line.split('\t')
    require(len(fields) == 5, 'source identity shape')
    archive, commit, path, blob, expected = fields
    safe_path(archive)
    safe_path(path)
    require(re.fullmatch(r'[0-9a-f]{40}', commit) and re.fullmatch(r'[0-9a-f]{40}', blob)
            and re.fullmatch(r'[0-9a-f]{64}', expected), 'source identity hashes')
    require((archive, path) not in identities, 'duplicate source identity')
    require(archive_commits.setdefault(archive, commit) == commit, 'one commit per source archive')
    identities[(archive, path)] = (commit, blob, expected)
source_bytes = {}
for archive in archive_commits:
    seen = set()
    regular = set()
    with tarfile.open(root / archive) as stream:
        for member in stream.getmembers():
            name = member.name.rstrip('/') if member.isdir() else member.name
            safe_path(name)
            require(name not in seen, 'duplicate archive member')
            seen.add(name)
            require(member.isdir() or member.isfile(), 'nonregular source member')
            if member.isdir():
                continue
            regular.add(name)
            key = (archive, name)
            require(key in identities, 'undeclared regular source member')
            data = stream.extractfile(member).read()
            commit, blob, expected = identities[key]
            require(sha(data) == expected, 'source SHA')
            header = f'blob {len(data)}\0'.encode()
            require(hashlib.sha1(header + data).hexdigest() == blob, 'exact Git blob identity')
            source_bytes[key] = data
            if args.git:
                repository = '/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388' \
                    if archive == 'source/producer389-codecs.tar' else \
                    '/home/sean/Documents/.cdk2-worktrees/disk-retained-four-epoch-observer-after626-ready'
                actual_blob = subprocess.check_output(['git', '-C', repository, 'rev-parse', f'{commit}:{name}']).decode().strip()
                actual_data = subprocess.check_output(['git', '-C', repository, 'show', f'{commit}:{name}'])
                require(actual_blob == blob and actual_data == data, 'archived actual signed Git source')
        require(regular == {path for candidate, path in identities if candidate == archive},
                'complete regular source archive closure')
require(len(archive_commits) == 5 and len(identities) == 48, 'finite selected source scope')
for archive, name in source_bytes:
    if archive == 'source/tested-a19.tar':
        require(source_bytes[(archive, name)] == source_bytes[('source/ready627.tar', name)],
                'tested/ready seven-source exact parity')

run = root / 'native/run-2'
result = json.loads((run / 'result.json').read_text())
require(result['failure'] is None and result['qemu_status'] == 3 and result['budget_seconds'] == 180
        and result['wall_seconds'] == 113.09554802400089, 'actual bounded native result')
require(result['epochs'] == [f'boot-{index}' for index in range(4)], 'four accepted epochs')
events = [event for event in result['events'] if event.get('event') == 'RESET']
require(len(events) == 3 and all(event.get('data', {}).get('guest') is True for event in events),
        'three actual guest resets')
before = json.loads((run / 'inputs-before.json').read_text())
after = json.loads((run / 'inputs-after.json').read_text())
require(len(before) == 25013 and before == after, 'actual original immutable 25013 maps')
require(result['disk_sha256'] == result['staged_disk_sha256'] ==
        '55da8777ab3a4c4ed2c3442573c6de1d67094671a20980bc7e9125674a0abcf1', 'staged disk equality')
require(result['installed_sha256'] == 'b63beab75efa2e014f87fa5858f1e564c1bcfe6918c23708bbb93c456a3f5249',
        'actual installed media hash')
uart = (run / 'serial.log').read_bytes()
for suffix, count in [('MAIN', 2), ('SETUP1_SECURE0', 2), ('REAL_FMP_ESRT_INFO', 2),
                      ('SET_READBACK_RESET', 1), ('TARGET_A_NO_RESUBMIT_PASS', 1)]:
    require(uart.count(('CDK2_EFI_DISK_REQUEST_' + suffix + '\r\n').encode()) == count,
            'actual app marker cardinality')
require(b'CDK2_EFI_DISK_REQUEST_FAIL' not in uart, 'app failure absent')
for index in range(4):
    body = (run / f'boot-{index}/cbmem-live.log').read_bytes()
    start = body.rfind(b'CDK2 | CAPSULE_RAM | begin')
    require(start >= 0, 'actual current RAM-begin boundary')
    body = body[start:]
    for operation in ['CHECK', 'SET']:
        prefix = f'CDK2 | module 0x02 | SystemFmp {operation} transport'.encode()
        matches = re.findall(rb'(?m)^' + re.escape(prefix) + rb' \| 0x([0-9a-f]{16})\r?$', body)
        expected = 1 if index == 2 else 0
        require(body.count(prefix) == len(matches) == expected, 'current epoch transport cardinality')
        if expected:
            require(matches == [b'0000000000000000'], 'actual successful transport')
for group in ['codec-author', 'codec-peer']:
    for optimization in [0, 2]:
        text = (root / group / f'check-o{optimization}.log').read_text()
        require(text.strip() == 'Actual producer codec: successful A history, cleared disk request, retired RAM request, clean FTW: PASS',
                'actual real-codec output')
for group in ['config-host', 'ready-host']:
    require((root / group / 'host.status').read_text().strip() == '0', 'HOST actual status')
    require('Ran 9 tests' in (root / group / 'host.log').read_text() and
            (root / group / 'host.log').read_text().rstrip().endswith('OK'), 'nine HOST cases')
require(not (root / 'ready-host/whole-tree-parity.diff').read_bytes(), 'ready complete tree parity')
require((root / 'native/outer-2.status').read_text().strip() == '0', 'actual successful outer status')
require((root / 'prior-preflight/outer.status').read_text().strip() == '1', 'preserved preflight status1')
require('actual packaged producer config differs' in (root / 'prior-preflight/native.log').read_text(),
        'preserved actual pre-QEMU failure')
for path in ['native/native-2.time', 'native/launch-2.time', 'corrected-compile/compile.time',
             'saved-author/replay.time', 'saved-peer/check.time', 'saved-peer/store.time']:
    require('EXIT=0' in (root / path).read_text(), 'actual recorded exit0: ' + path)
print(f'Finite receipt PASS: {len(manifest)} files, {len(copied)} original copies, {len(identities)} actual Git blobs')
print('One actual four-boot positive; saved/HOST checks only; no new VM or omitted-media reconstruction')
