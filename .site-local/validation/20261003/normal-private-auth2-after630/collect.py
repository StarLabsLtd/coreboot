#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Collect only finite public AUTH2 receipts; never launch a build or guest."""

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent
HOME = Path('/home/sean')
TEST = HOME / 'Documents/.cdk2-worktrees/normal-private-auth2-older-append-after628'
PRODUCER = HOME / 'Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388'
GX = HOME / 'normal-private-auth2-native-registry-after628.GXO9JR'
OLD = HOME / 'native-default-zero-hotkey-after628.CfqEvm'
TESTED = '796022ad0979f627773611fa1101449a7cc86b30'
READY = '6a990c61dd1830d8158645a081660b85090a64f1'
ORIGINAL = 'd8e49af2ab0c90c6f6542b29026bd80309a9e1da'
PRODUCER_HEAD = '7ee34bed989c46913c3ee6672fb25e83227c3b6c'
PATHS = ['Makefile', 'src/boot/Makefile', 'tests/normal_private_auth2_host_test.sh',
         'tests/protected_variable_fullgraph_runtime_app.c',
         'tests/protected_variable_fullgraph_enrolled_inputs.sh',
         'tests/protected_variable_private_media_check.c',
         'tests/protected_variable_private_media_check.sh']
TEXT_SUFFIXES = {'.log', '.time', '.sha256', '.mk', '.list', '.txt', '.status'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    out = args.destination.resolve()
    out.mkdir(exist_ok=False)
    originals = []
    sources = []

    def copy(source, target):
        if source.is_symlink() or not source.is_file() or target in {row[0] for row in originals}:
            raise ValueError('nonregular or duplicate original: ' + str(source))
        before = source.read_bytes()
        destination = out / target
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        if destination.read_bytes() != before or source.read_bytes() != before:
            raise ValueError('original changed during finite copy')
        originals.append((target, str(source), sha(before)))

    def selected(directory, label, names=None):
        files = [directory / name for name in names] if names is not None else \
            sorted(path for path in directory.iterdir() if path.suffix in TEXT_SUFFIXES)
        for path in files:
            copy(path, label + '/' + path.name)

    for name in ('README.md', 'check-receipts.py', 'collect.py'):
        copy(HERE / name, name)
    for name in ('normal-private-auth2-native-after628.1BnXYZ',
                 'normal-private-auth2-native-include-after628.7zb5k6',
                 'normal-private-auth2-native-registry-after628.GXO9JR'):
        directory = HOME / name
        label = 'receipts/' + name
        selected(directory, label)
        selected(directory / 'app', label + '/app')
        selected(directory, label, ['build-app.sh', 'run-native.py'])
        if directory == GX:
            selected(directory, label, ['run-native-retry.py', 'terminal-exit-host-test.py'])
        if (directory / 'app/native/normal-private-auth2-inputs.log').is_file():
            copy(directory / 'app/native/normal-private-auth2-inputs.log', label + '/app/CMS-inputs.log')
    for name, internal in (
            ('normal-private-auth2-host-after628.XFcdT8', 'normal-private-auth2-HOST.c6jAiB'),
            ('normal-private-auth2-independent.oyytDf', 'normal-private-auth2-HOST.8Z6byF'),
            ('normal-private-auth2-host-include-after628.6VFVXb', 'normal-private-auth2-HOST.k2mTtW'),
            ('normal-private-auth2-host-include-independent.jaw0Gs', 'normal-private-auth2-HOST.Qv6LZF')):
        directory = HOME / name
        selected(directory, 'host/' + name)
        selected(directory / internal, 'host/' + name + '/internal')
    for name in ('normal-private-auth2-terminal-host.TTRvZA',
                 'normal-private-auth2-terminal-final-host.k4KKpW'):
        selected(HOME / name, 'host/' + name)
    peer = HOME / 'normal-auth2-independent.bGC8xk'
    selected(peer, 'independent')
    selected(peer / 'media/private-media.uqjME2', 'independent/media')
    for number in (1, 2):
        directory = GX / f'run-{number}'
        label = f'native/run-{number}'
        selected(directory, label, ['result.json', 'command.json', 'inputs-before.json',
                                   'inputs-after.json', 'serial.log', 'qemu.log', 'cbmem-live.log',
                                   'cbmem-observer.json', 'cbmem-console.bin', 'cbmem-table-0.bin',
                                   'cbmem-table-1.bin', 'packaged-producer.config'])
    copy(GX / 'run-2/private-media.log', 'native/run-2/private-media.log')
    selected(GX / 'run-2/private-media/private-media.2WDW9J', 'native/run-2/media')
    selected(OLD, 'original-normal628', ['build-core.sh', 'build-producer.sh', 'build.log', 'build.time',
                                       'resolved.config', 'source-head.txt', 'source-after.status',
                                       'source-before.sha256', 'source-after-check.log', 'inputs-before.sha256',
                                       'outputs.sha256'])
    copy(OLD / 'include/cdk2/config.h', 'original-normal628/config.h')
    copy(OLD / 'native/native-direct-image-inventory.tsv', 'original-normal628/image-inventory.tsv')
    selected(OLD / 'initial9', 'original-normal628/producer389',
             ['full.config', 'build.time', 'source-before.sha256', 'outputs.sha256'])

    def archive(repo, commit, paths, label):
        if git(repo, 'show', '-s', '--format=%G?', commit).strip() != b'G':
            raise ValueError('source commit signature is not verified good')
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode='w') as tar:
            for path in sorted(paths):
                body = git(repo, 'show', commit + ':' + path)
                blob = git(repo, 'rev-parse', commit + ':' + path).decode().strip()
                member = tarfile.TarInfo(path)
                member.size, member.mode, member.mtime = len(body), 0o644, 0
                tar.addfile(member, io.BytesIO(body))
                sources.append((label, str(repo), commit, path, blob, sha(body)))
        destination = out / label
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(gzip.compress(buffer.getvalue(), mtime=0))

    for commit, name in ((TESTED, 'tested796'), (READY, 'ready630'),
                         ('f48e634a34cff2febb2d624e2dd03eceb1540967', 'config-f48'),
                         ('4df1b4f47b3e36af7eb3ec2029d21c335d9dbbad', 'first-4df')):
        archive(TEST, commit, PATHS, 'source/' + name + '.tar.gz')
    archive(TEST, ORIGINAL, [path for path in PATHS if path != 'tests/normal_private_auth2_host_test.sh'],
            'source/original628-six.tar.gz')
    helpers = ['util/qemu/bin/qmp_cbmem_console.py', 'util/qemu/bin/run-deadline.py',
               'tests/system_fmp_core_ram_native_test.py', 'tests/system_fmp_disk_retained_native_test.py']
    archive(TEST, TESTED, helpers, 'source/tested-observer-helpers.tar.gz')
    closure = GX / 'run-2/private-media/private-media.2WDW9J/closure-before.sha256'
    codec_paths = []
    for line in closure.read_text().splitlines():
        path = Path(line.split('  ', 1)[1])
        if path.is_relative_to(PRODUCER):
            codec_paths.append(path.relative_to(PRODUCER).as_posix())
    archive(PRODUCER, PRODUCER_HEAD, sorted(set(codec_paths)), 'source/actual-codec389.tar.gz')
    for path in PATHS:
        if git(TEST, 'show', READY + ':' + path) != git(TEST, 'show', TESTED + ':' + path):
            raise ValueError('ready630 changed a tested path body')
    (out / 'source-identities.tsv').write_text(''.join('\t'.join(row) + '\n' for row in sources))
    (out / 'originals.tsv').write_text(''.join('\t'.join(row) + '\n' for row in originals))
    result = json.loads((GX / 'run-2/result.json').read_text())
    metadata = {'tested_head': TESTED, 'ready_head': READY, 'original_firmware_head': ORIGINAL,
                'producer_head': PRODUCER_HEAD, 'native_session': 73762,
                'wall_seconds': result['wall_seconds'], 'budget_seconds': 180,
                'original_core_sha256': result['original_core_sha256'],
                'original_rom_sha256': result['original_rom_sha256'],
                'inputs_count': len(json.loads((GX / 'run-2/inputs-before.json').read_text())),
                'independent_session': 44467, 'private_inputs_omitted': True}
    (out / 'metadata.json').write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n')
    files = sorted(path for path in out.rglob('*') if path.is_file())
    (out / 'files.sha256').write_text(''.join(sha(path.read_bytes()) + '  ' +
                                           path.relative_to(out).as_posix() + '\n' for path in files))
    print(f'Collected {len(files)} manifest records, {len(originals)} original copies, {len(sources)} Git blobs')


if __name__ == '__main__':
    main()
