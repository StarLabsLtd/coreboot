#!/usr/bin/env python3
"""Root-only finite completed retirement receipts; no payload traversal."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys

LOOSE = Path('/home/sean/loose-cdk2-files-retirement-final.20261004-r1')
SEA = Path('/home/sean/seabios53-clone-retirement-final.20261004-r1')
PUBLISHED = '296aaee198c6db3c956bff6afb9a73ff092dcc12'
PINNED = [
    ('/home/sean/loose-cdk2-files-retirement-plan.7JufYw/retire.py', 'a3024ed4cbc1fe0946e7be96fb96105ea41235d736ec783bc7a24fac07ed5454'),
    ('/home/sean/loose-cdk2-files-retirement-plan.7JufYw/README.txt', 'ca4ec19c5d0af69e0075107c206f8054dafa56dcb58dd809974d23461050e106'),
    ('/home/sean/seabios53-clone-retirement-plan.wTHIKx/retire.py', 'bc447b2693d81a31497c1a031944108eb4b3bb82f29d618c19185b68661d3c33'),
    ('/home/sean/seabios53-clone-retirement-plan.wTHIKx/README.txt', 'af9ced391763310abc882aaea6d5dddf66861119d1f72c0dffacdb3f00eb00b4')]

def require(value, message):
    if not value:
        raise RuntimeError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def regular(path):
    require(path.is_file() and not path.is_symlink() and path.resolve() == path and
            stat.S_ISREG(path.stat().st_mode), 'nonregular selected input: ' + str(path))

def expected_names(groups, extra):
    return {f'{kind}-{i}.{suffix}' for kind, count in groups for i in range(1, count + 1)
            for suffix in ('command', 'log', 'status')} | set(extra)

LOOSE_NAMES = expected_names((('move', 3), ('trash', 144)),
    ('retirement.status', 'published-head.txt', 'original-identities.json',
     'tools-before.json', 'completed.json', 'QUALIFICATION.txt'))
SEA_NAMES = expected_names((('trash', 53),),
    ('retirement.status', 'published-head.txt', 'original-identities.json',
     'tools-before.json', 'completed.json', 'QUALIFICATION.txt',
     'incoming-graph-before.json', 'incoming-graph-after.json'))

def files(root, expected):
    require(root.is_dir() and root.resolve() == root, 'invalid exact receipt directory')
    paths = sorted(root.iterdir())
    require({p.name for p in paths} == expected, 'receipt inventory differs: ' + str(root))
    for path in paths:
        regular(path)
    return paths

def admission():
    for root, names, count in ((LOOSE, LOOSE_NAMES, 147), (SEA, SEA_NAMES, 53)):
        paths = files(root, names)
        require(all(p.read_bytes() == b'0\n' for p in paths if p.suffix == '.status'),
                'operation incomplete or failed: ' + str(root))
        require((root / 'published-head.txt').read_text() == PUBLISHED + '\n',
                'wrong actual publication binding')
        completed = json.loads((root / 'completed.json').read_text())
        require(len(completed) == count and len({r['source'] for r in completed}) == count,
                'wrong completed identity cardinality')
        # Read receipt identities only. Never follow payload paths into trash/archives.
        for row in completed:
            require(isinstance(row['source'], str) and isinstance(row['target'], str) and
                    isinstance(row['identity'], dict), 'incomplete operation identity')
        if root == SEA:
            require(all(r['disposition'] == 'recoverable-clone-trash' for r in completed),
                    'unexpected clone disposition')
            for name in ('incoming-graph-before.json', 'incoming-graph-after.json'):
                graph = json.loads((root / name).read_text())
                require(isinstance(graph['markers'], list) and isinstance(graph['alternates'], dict),
                        'missing recorded clone graph')

def tools():
    rows = []
    for name in ('python3', 'gzip', 'sha256sum'):
        alias = shutil.which(name)
        require(alias is not None, 'missing enumerated utility')
        actual = Path(alias).resolve()
        regular(actual)
        rows.append({'name': name, 'alias': alias, 'actual': str(actual),
                     'sha256': sha(actual.read_bytes())})
    return rows

def main():
    require(len(sys.argv) == 2, 'usage: collect.py ABSENT_ABSOLUTE_PACKET')
    require(os.environ.get('PYTHONDONTWRITEBYTECODE') == '1', 'disable bytecode')
    destination = Path(sys.argv[1])
    require(destination.is_absolute() and not destination.exists() and not destination.is_symlink()
            and destination.parent.is_dir() and destination.parent.resolve() == destination.parent,
            'packet must be absent under resolved existing parent')
    admission()  # Refuse active53 receipts and any partial operation.
    selected = [(p, 'loose147/' + p.name) for p in files(LOOSE, LOOSE_NAMES)]
    selected += [(p, 'seabios53/' + p.name) for p in files(SEA, SEA_NAMES)]
    for spelling, digest in PINNED:
        path = Path(spelling)
        regular(path)
        require(sha(path.read_bytes()) == digest, 'executed recipe changed')
        selected.append((path, 'recipes/' + path.parent.name + '/' + path.name))
    own = Path(__file__).resolve().parent
    selected += [(own / name, 'collector/' + name) for name in ('collect.py', 'README.txt')]
    snapshots = [(p, rel, p.read_bytes()) for p, rel in selected]
    require(len({str(p) for p, rel, data in snapshots}) == len(snapshots) and
            len({rel for p, rel, data in snapshots}) == len(snapshots), 'duplicate source/map')
    headers = {b'-----BEGIN PRIVATE KEY-----', b'-----BEGIN OPENSSH PRIVATE KEY-----',
               b'-----BEGIN RSA PRIVATE KEY-----', b'-----BEGIN EC PRIVATE KEY-----',
               b'-----BEGIN DSA PRIVATE KEY-----', b'-----BEGIN ENCRYPTED PRIVATE KEY-----'}
    for path, relative, data in snapshots:
        regular(path)
        require(b'\0' not in data, 'nontext receipt refused')
        require(not any(line in headers for line in data.splitlines()), 'private key header refused')
    before = tools()
    destination.mkdir(mode=0o755)
    (destination / 'collection.status').write_text('1\n')
    try:
        mappings, originals = [], []
        for source, relative, data in snapshots:
            mode = 'plain'
            if len(data) > 1024 * 1024:
                relative += '.gz'
                mode = 'gzip-n'
            stored = destination / relative
            stored.parent.mkdir(parents=True, exist_ok=True)
            if mode == 'plain':
                stored.write_bytes(data)
                require(stored.read_bytes() == data, 'stored plain bytes differ')
            else:
                with stored.open('wb') as output:
                    subprocess.run(['gzip', '-n', '-c'], input=data, stdout=output, check=True)
                require(gzip.decompress(stored.read_bytes()) == data, 'stored gzip bytes differ')
            mappings.append(str(source) + '\t' + relative + '\t' + mode + '\n')
            originals.append(sha(data) + '  ' + str(source) + '\n')
        (destination / 'FILES_MAP.tsv').write_text(''.join(mappings))
        (destination / 'ORIGINAL_SHA256SUMS').write_text(''.join(originals))
        (destination / 'TOOLS.json').write_text(json.dumps(before, indent=2) + '\n')
        (destination / 'QUALIFICATION.txt').write_text(
            'Completed loose147 and SeaBIOS53 retirement receipts only; no HOST/firmware/VM/hardware claim.\n'
            'Loose147: three same-inode retained local archive renames plus144 recoverable file trash operations.\n'
            'Excluded2 active scripts were checked inline by operation, but no separate before identity ledger was persisted.\n'
            'SeaBIOS53: recoverable clone-leaf trash only, not parent removals; generated clone bodies are not copied here.\n'
            'Exact flat command/log/status/identity/graph receipts plus four executed recipe files and this collector.\n'
            'No trash payload/archive/ELF/media/vendor/source/Git-object traversal or old-original SHA replay after moves.\n'
            'Actual operations bind signed published296aa; archival does not rerun operations or reconstruction helpers.\n'
            'Utility closure is finite enumeration; originals and deterministic plain/gzip counterparts remain byte-exact.\n')
        admission()
        for source, relative, data in snapshots:
            require(source.read_bytes() == data, 'selected original changed')
        require(tools() == before, 'enumerated utilities changed')
        subprocess.run(['sha256sum', '--quiet', '-c', str(destination / 'ORIGINAL_SHA256SUMS')], check=True)
        (destination / 'collection.status').write_text('0\n')
        ledger = []
        for path in sorted(destination.rglob('*')):
            require(not path.is_symlink(), 'packet symlink')
            if path.is_file():
                ledger.append(sha(path.read_bytes()) + '  ' + str(path.relative_to(destination)) + '\n')
        (destination / 'ARCHIVE_SHA256SUMS').write_text(''.join(ledger))
        subprocess.run(['sha256sum', '--quiet', '-c', 'ARCHIVE_SHA256SUMS'], cwd=destination, check=True)
        print('PASS finite retirement receipt originals=' + str(len(snapshots)), flush=True)
    except BaseException:
        (destination / 'collection.status').write_text('1\n')
        raise

if __name__ == '__main__':
    main()
