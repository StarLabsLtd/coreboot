#!/usr/bin/env python3
"""Root-only finite cleanup receipts collector; no source replay or deletion."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys

OLD = Path('/home/sean/coreboot-retirement66-final.20261004-r1')
NEW = Path('/home/sean/coreboot-retirement66-continuation25.20261004-r1')
SUMMARY = Path('/home/sean/coreboot-retirement25-Tc-preflight-failure.txt')
SUMMARY_SHA = 'e32011cd0f7b76f72761ca5edebc8058aa525f7120896d4deec4ae565b4d1f14'
PINNED = [
    ('/home/sean/coreboot-retirement66-retry-proposal.B1TKcb/retire.py', 'c6c729d8817ec1eb07d4fc974cec510fb120a804e7168cfc9d8a84cedbd174ea'),
    ('/home/sean/coreboot-retirement66-retry-proposal.B1TKcb/README.txt', '6cf8c28d7e491bf3626e9b210c089e23d3e7648f1926cfe6016a0c7c13bef447'),
    ('/home/sean/coreboot-retirement25-common-retry-proposal.TcPYmQ/retire.py', '28b3870fb7a7734a8afface7e333c228a0fccc2c4ba5767e58f4726593bfb5b3'),
    ('/home/sean/coreboot-retirement25-common-retry-proposal.TcPYmQ/README.txt', 'a09f155efba37f978416afe5f736854f4354a2435a9eb68a51ea2eeea3a08755'),
    ('/home/sean/coreboot-retirement25-owner-retry-proposal.HENnUt/retire.py', 'cbfa469848632c044d9e31f30be7055fa4dea7b1f852cbb490c3e3dce85e1bc8'),
    ('/home/sean/coreboot-retirement25-owner-retry-proposal.HENnUt/README.txt', 'af71b51368a57e0593c7d859237057a153226fa48d14b44d60ba0fb6c1d7d967')]
SUFFIXES = {'.log', '.status', '.command', '.json', '.txt'}

def require(value, message):
    if not value:
        raise RuntimeError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def regular(path):
    require(not path.is_symlink() and path.is_file() and
            stat.S_ISREG(path.stat().st_mode) and path.resolve() == path,
            'nonregular selected file: ' + str(path))

def files(root):
    require(root.is_dir() and root.resolve() == root, 'not a proper finite receipt directory')
    found = []
    for path in sorted(root.iterdir()):
        # Flat selected receipt roots only: no source/build/Git/object traversal.
        regular(path)
        require(path.suffix in SUFFIXES, 'unqualified receipt suffix')
        found.append(path)
    return found

def statuses():
    expected_old = {f'remove-{i}.status': '0\n' for i in range(1, 42)}
    expected_old.update({'retirement.status': '1\n', 'stop-diagnostic.status': '128\n'})
    expected_new = {f'remove-{i}.status': '0\n' for i in range(42, 67)}
    expected_new['retirement.status'] = '0\n'
    for root, expected in ((OLD, expected_old), (NEW, expected_new)):
        actual = {p.name: p.read_text() for p in files(root) if p.suffix == '.status'}
        require(actual == expected, 'actual raw receipt statuses differ')
    for root, numbers in ((OLD, range(1, 42)), (NEW, range(42, 67))):
        for index in numbers:
            require((root / f'remove-{index}.command').is_file() and
                    (root / f'remove-{index}.log').is_file(), 'missing actual removal command/log')
    for filename in ('incoming-graph-before.json', 'incoming-graph-after.json'):
        regular(NEW / filename)
        graph = json.loads((NEW / filename).read_text())
        require(isinstance(graph['markers'], list) and isinstance(graph['alternates'], dict),
                'missing actual final graph')
    require((OLD / 'pflash-mbedtls-alternates-before.txt').read_text() ==
            '/home/sean/Documents/coreboot/.git/worktrees/q35-fresh-root-inventory-after383/modules/3rdparty/mbedtls/objects\n',
            'old pointer record differs')
    require((OLD / 'pflash-mbedtls-alternates-after.txt').read_text() ==
            '/home/sean/Documents/coreboot/.git/modules/3rdparty/mbedtls/objects\n',
            'repaired pointer record differs')

def tools():
    result = []
    for name in ('python3', 'gzip', 'sha256sum'):
        alias = shutil.which(name)
        require(alias is not None, 'missing enumerated utility')
        actual = Path(alias).resolve()
        regular(actual)
        result.append({'name': name, 'alias': alias, 'actual': str(actual),
                       'sha256': sha(actual.read_bytes())})
    return result

def main():
    require(len(sys.argv) == 2, 'usage: collect.py ABSENT_ABSOLUTE_PACKET')
    require(os.environ.get('PYTHONDONTWRITEBYTECODE') == '1', 'disable bytecode')
    destination = Path(sys.argv[1])
    require(destination.is_absolute() and not destination.exists() and not destination.is_symlink()
            and destination.parent.is_dir() and destination.parent.resolve() == destination.parent,
            'packet must be absent under resolved existing parent')
    statuses()  # Continuation must have finished successfully; never collect active receipts.
    regular(SUMMARY)
    require(sha(SUMMARY.read_bytes()) == SUMMARY_SHA, 'Tc failure qualification changed')
    selected = [(p, 'partial41/' + p.name) for p in files(OLD)]
    selected += [(p, 'continuation25/' + p.name) for p in files(NEW)]
    selected.append((SUMMARY, 'preflight-failure/' + SUMMARY.name))
    for spelling, expected in PINNED:
        source = Path(spelling)
        regular(source)
        require(sha(source.read_bytes()) == expected, 'executed recipe changed')
        selected.append((source, 'recipes/' + source.parent.name + '/' + source.name))
    own = Path(__file__).resolve().parent
    selected += [(own / name, 'collector/' + name) for name in ('collect.py', 'README.txt')]
    snapshots = [(p, relative, p.read_bytes()) for p, relative in selected]
    require(len({str(p) for p, relative, data in snapshots}) == len(snapshots) and
            len({relative for p, relative, data in snapshots}) == len(snapshots), 'duplicate source/map')
    for path, relative, data in snapshots:
        regular(path)
        require(b'\0' not in data, 'binary selected receipt')
        require(b'-----BEGIN PRIVATE KEY-----' not in data and
                b'-----BEGIN OPENSSH PRIVATE KEY-----' not in data, 'private key refused')
    before_tools = tools()
    destination.mkdir(mode=0o755)
    (destination / 'collection.status').write_text('1\n')
    maps = []
    originals = []
    for source, relative, data in snapshots:
        mode = 'plain'
        if len(data) > 1024 * 1024:
            relative += '.gz'
            mode = 'gzip-n'
        stored = destination / relative
        stored.parent.mkdir(parents=True, exist_ok=True)
        if mode == 'plain':
            stored.write_bytes(data)
            require(stored.read_bytes() == data, 'plain byte mismatch')
        else:
            with stored.open('wb') as output:
                subprocess.run(['gzip', '-n', '-c'], input=data, stdout=output, check=True)
            require(gzip.decompress(stored.read_bytes()) == data, 'gzip counterpart differs')
        maps.append(str(source) + '\t' + relative + '\t' + mode + '\n')
        originals.append(sha(data) + '  ' + str(source) + '\n')
    (destination / 'FILES_MAP.tsv').write_text(''.join(maps))
    (destination / 'ORIGINAL_SHA256SUMS').write_text(''.join(originals))
    (destination / 'TOOLS.json').write_text(json.dumps(before_tools, indent=2) + '\n')
    (destination / 'QUALIFICATION.txt').write_text(
        'Cleanup/reconstruction receipts only, no firmware/QEMU/hardware or historical HOST gate claim.\n'
        'Original41-removal receipt remains failure1; stop128 is separate read-only diagnostic replay.\n'
        'One pointer repaired to canonical retained mbedtls; original/new bytes preserved.\n'
        'Tc attempted continuation failed pre-output admission, no receipt/no removals: summary is NOT raw terminal log.\n'
        'New owner-set continuation25 statuses0 is separate; original failure is NOT reclassified.\n'
        'Only flat text receipts, exact six executed/attempted recipe files and collector bodies copied.\n'
        'No source/vendor/Git object/build/ROM/media/private state payload copied; no source SHA replay after deletion.\n')
    statuses()
    require(files(OLD) + files(NEW) == [p for p, rel in selected
            if p.parent in (OLD, NEW)], 'receipt file selection changed')
    for source, relative, data in snapshots:
        require(source.read_bytes() == data, 'selected original changed during collection')
    require(tools() == before_tools, 'enumerated utility changed')
    subprocess.run(['sha256sum', '--quiet', '-c', str(destination / 'ORIGINAL_SHA256SUMS')], check=True)
    try:
        (destination / 'collection.status').write_text('0\n')
        ledger = []
        for path in sorted(destination.rglob('*')):
            require(not path.is_symlink(), 'packet symlink')
            if path.is_file():
                ledger.append(sha(path.read_bytes()) + '  ' + str(path.relative_to(destination)) + '\n')
        (destination / 'ARCHIVE_SHA256SUMS').write_text(''.join(ledger))
        subprocess.run(['sha256sum', '--quiet', '-c', 'ARCHIVE_SHA256SUMS'], cwd=destination, check=True)
        print('PASS finite originals=' + str(len(snapshots)) + '; partial41 failure and continuation25 separate')
    except BaseException:
        (destination / 'collection.status').write_text('1\n')
        raise

if __name__ == '__main__':
    main()
