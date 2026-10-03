#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Portable archival and actual saved-CBMEM oracle; no build, guest or media writer."""

import argparse
import ast
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import tarfile
import uuid


def require(condition, message):
    if not condition:
        raise ValueError(message)


def safe_path(value):
    path = PurePosixPath(value)
    require(value and not path.is_absolute() and str(path) == value and
            all(part not in ('.', '..') for part in path.parts), 'unsafe relative archive path')
    return value


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--originals', action='store_true', help='compare preserved host originals, if present')
    parser.add_argument('--git', action='store_true', help='compare exact source commits in their original repos')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    expected = {}
    for line in (root / 'files.sha256').read_text().splitlines():
        digest, path = line.split('  ', 1)
        require(re.fullmatch('[0-9a-f]{64}', digest), 'bad file digest')
        safe_path(path)
        require(path not in expected, 'duplicate manifest path')
        expected[path] = digest
    entries = list(root.rglob('*'))
    require(all(not path.is_symlink() and (path.is_file() or path.is_dir()) for path in entries),
            'packet contains a nonregular or symlink entry')
    actual = {path.relative_to(root).as_posix() for path in entries
              if path.is_file() and path != root / 'files.sha256'}
    require(set(expected) == actual, 'manifest must cover the complete finite packet')
    for path, digest in expected.items():
        file = root / path
        require(not file.is_symlink() and sha(file.read_bytes()) == digest, 'packet SHA mismatch: ' + path)
    original_rows = []
    for line in (root / 'originals.tsv').read_text().splitlines():
        target, original, digest = line.split('\t')
        require(target in expected and expected[target] == digest and Path(original).is_absolute(),
                'original-copy map does not match the packet manifest')
        original_rows.append((target, original, digest))
    require(len({row[0] for row in original_rows}) == len(original_rows), 'duplicate original-copy target')
    if args.originals:
        for _, original, digest in original_rows:
            require(sha(Path(original).read_bytes()) == digest, 'changed original: ' + original)
    sources = {}
    source_rows = []
    for line in (root / 'source-identities.tsv').read_text().splitlines():
        archive, repo, commit, path, blob, digest = line.split('\t')
        safe_path(archive)
        safe_path(path)
        require(archive in expected and Path(repo).is_absolute() and re.fullmatch('[0-9a-f]{40}', commit) and
                re.fullmatch('[0-9a-f]{40}', blob) and re.fullmatch('[0-9a-f]{64}', digest),
                'malformed source identity')
        require((archive, path) not in sources, 'duplicate archive/source path')
        sources[(archive, path)] = digest
        source_rows.append((archive, repo, commit, path, blob, digest))
    contents = {}
    for archive in sorted({row[0] for row in source_rows}):
        rows = [row for row in source_rows if row[0] == archive]
        require(len({(row[1], row[2]) for row in rows}) == 1, 'mixed source identities in one archive')
        with tarfile.open(root / archive, 'r:gz') as tar:
            members = tar.getmembers()
            require(all(member.isfile() for member in members), 'source archive contains nonregular members')
            names = [safe_path(member.name) for member in members]
            require(len(names) == len(set(names)), 'duplicate source archive member')
            require(set(names) == {row[3] for row in rows}, 'source archive membership mismatch')
            for member in members:
                body = tar.extractfile(member).read()
                require(sha(body) == sources[(archive, member.name)], 'source member SHA mismatch')
                contents[(archive, member.name)] = body
    if args.git:
        for archive, repo, commit, path, blob, digest in source_rows:
            actual_blob = subprocess.check_output(['git', '-C', repo, 'rev-parse', commit + ':' + path]).decode().strip()
            body = subprocess.check_output(['git', '-C', repo, 'show', commit + ':' + path])
            require(actual_blob == blob and sha(body) == digest, 'source Git identity mismatch')
    tested = {path: body for (archive, path), body in contents.items() if archive == 'source/tested796.tar.gz'}
    ready = {path: body for (archive, path), body in contents.items() if archive == 'source/ready630.tar.gz'}
    require(len(tested) == 7 and tested == ready, 'ready630 must have exactly the seven tested bodies')
    gx = root / 'receipts/normal-private-auth2-native-registry-after628.GXO9JR'
    source = (gx / 'run-native-retry.py').read_text()
    tree = ast.parse(source)
    saved = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'check_saved']
    markers = [node for node in tree.body if isinstance(node, ast.Assign) and
               any(isinstance(target, ast.Name) and target.id == 'MARKERS' for target in node.targets)]
    require(len(saved) == len(markers) == 1, 'exact actual saved-oracle AST required')
    reader = contents[('source/tested-observer-helpers.tar.gz', 'util/qemu/bin/qmp_cbmem_console.py')]
    namespace = {'__name__': 'packet_actual_saved_reader'}
    exec(compile(reader, 'signed-qmp_cbmem_console.py', 'exec'), namespace)
    namespace.update(json=json, re=re, struct=struct, uuid=uuid, MARKERS=ast.literal_eval(markers[0].value))
    exec(compile(ast.Module(body=saved, type_ignores=[]), 'actual-run-native-retry-check_saved', 'exec'), namespace)
    namespace['check_saved'](root / 'native/run-2')
    try:
        namespace['check_saved'](root / 'native/run-1')
    except ValueError as error:
        require('real guest success' in str(error), 'original run1 failed for an unexpected saved-oracle reason')
    else:
        raise ValueError('original failed run1 must not be relabeled success')
    for number in (1, 2):
        run = root / f'native/run-{number}'
        before = json.loads((run / 'inputs-before.json').read_text())
        after = json.loads((run / 'inputs-after.json').read_text())
        require(len(before) == 26567 and before == after, 'native immutable input map mismatch')
    result = json.loads((root / 'native/run-2/result.json').read_text())
    metadata = json.loads((root / 'metadata.json').read_text())
    require(result['wall_seconds'] == metadata['wall_seconds'] and metadata['tested_head'] == result['test_head'] and
            result['original_firmware_head'] == metadata['original_firmware_head'] and
            result['original_core_sha256'] == metadata['original_core_sha256'] and
            result['original_rom_sha256'] == metadata['original_rom_sha256'], 'packet metadata/native binding mismatch')
    require(result['staged_disk_sha256'] == result['after_disk_sha256'], 'native disk baseline changed')
    profile = (root / 'original-normal628/config.h').read_text().splitlines()
    for name, value in (('PROTECTED_VARIABLE_RUNTIME', 1), ('NATIVE_SYSTEM_FMP', 1), ('NATIVE_SECURITY_STUB', 1),
                        ('NATIVE_QEMU_TEST_FMP', 0), ('QEMU_ACCEPTANCE_PROFILE', 0), ('BUILD_DEBUG', 0),
                        ('BOOT_TIMEOUT', 0), ('SECURE_BOOT', 0)):
        require(f'#define CONFIG_CDK2_{name} {value}' in profile, 'wrong original normal profile')
    closure = (gx / 'app/compile-closure.mk').read_text().replace('\\\n', ' ')
    require(len(re.findall(r'^closure:', closure, re.M)) == 13, 'actual app/marker/helper TU count is not thirteen')
    require('src/lib/pe_relocation_marker.c' in closure, 'real marker source missing from compile-input closure')
    for base, expected_status in (
            ('receipts/normal-private-auth2-native-after628.1BnXYZ/app-build-outer.time', 1),
            ('receipts/normal-private-auth2-native-include-after628.7zb5k6/app-build-outer.time', 2),
            ('receipts/normal-private-auth2-native-registry-after628.GXO9JR/app-build-outer.time', 0),
            ('receipts/normal-private-auth2-native-registry-after628.GXO9JR/native.time', 1),
            ('receipts/normal-private-auth2-native-registry-after628.GXO9JR/native-retry.time', 0),
            ('host/normal-private-auth2-terminal-host.TTRvZA/host.time', 1),
            ('host/normal-private-auth2-terminal-final-host.k4KKpW/host.time', 0),
            ('independent/saved.time', 0), ('independent/media.time', 0)):
        require(re.search(r'\bEXIT=' + str(expected_status) + r'\b', (root / base).read_text()),
                'preserved raw outcome mismatch: ' + base)
    require('Ran 17 tests' in (root / 'host/normal-private-auth2-terminal-final-host.k4KKpW/host.log').read_text(),
            'final terminal guard cardinality changed')
    print(f'PASS {len(expected)} packet SHA records; {len(original_rows)} copies; {len(source_rows)} signed-source identities')
    print('Actual saved FW9/phase/13-UART/noRESET/guest3 replay passed; final-media logs archived, not rerun without omitted media')


if __name__ == '__main__':
    main()
