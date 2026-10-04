#!/usr/bin/env python3
"""Root-only, exact archived 66-worktree retirement; no branch/object pruning."""
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys

EVIDENCE = Path('/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001')
DATE = EVIDENCE / '.site-local/validation/20261004'
PACKET = DATE / 'coreboot-config-retirement66-allrefs-r1'
VENDOR = DATE / 'vendor-recovery18'
PUBLISHED = 'f3fc8485d69ba9b4ebdac374a4f5b34dbb28a8b8'
COLLECTOR_SHA = '95a972dbd9c86a7513d381e02853d1f8634d8967da7b00d8c043501ba6f8bbe1'
ADVANCES = {'refs/heads/agent/validation-evidence-20261001',
            'refs/remotes/github/agent/validation-evidence-20261001'}

def require(value, message):
    if not value:
        raise RuntimeError(message)

def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args],
                                   stderr=subprocess.STDOUT)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def packet_check(packet, stored_count, map_count):
    relative = str(packet.relative_to(EVIDENCE))
    require(not git(EVIDENCE, 'status', '--porcelain=v1', '--untracked-files=all',
                    '--', relative), 'durable packet changed')
    files = sorted(p for p in packet.rglob('*') if p.is_file())
    require(not any(p.is_symlink() for p in packet.rglob('*')), 'packet symlink')
    tracked = git(EVIDENCE, 'ls-files', '-z', '--', relative).split(b'\0')
    require(set(filter(None, tracked)) ==
            {os.fsencode(str(p.relative_to(EVIDENCE))) for p in files},
            'packet has untracked or missing files')
    ledger = (packet / 'ARCHIVE_SHA256SUMS').read_text().splitlines()
    require(len(ledger) == stored_count and len(files) == stored_count + 1,
            'unexpected finite packet count')
    subprocess.run(['sha256sum', '--quiet', '-c', 'ARCHIVE_SHA256SUMS'],
                   cwd=packet, check=True)
    subprocess.run(['sha256sum', '--quiet', '-c', str(packet / 'ORIGINAL_SHA256SUMS')],
                   check=True)
    mapped = (packet / 'FILES_MAP.tsv').read_text().splitlines()
    require(len(mapped) == map_count, 'unexpected original mapping count')
    for row in mapped:
        original, stored, kind = row.split('\t')
        require(kind == 'plain' and not Path(stored).is_absolute() and
                '..' not in Path(stored).parts, 'unexpected mapping')
        source = Path(original)
        require(source.is_file() and not source.is_symlink(), 'original not regular')
        require(source.read_bytes() == (packet / stored).read_bytes(), 'original differs')

def main():
    require(len(sys.argv) == 2, 'usage: retire.py ABSENT_ABSOLUTE_RECEIPT')
    require(os.environ.get('PYTHONDONTWRITEBYTECODE') == '1', 'bytecode must be disabled')
    receipt = Path(sys.argv[1])
    require(receipt.is_absolute() and not receipt.exists() and not receipt.is_symlink()
            and receipt.parent.is_dir() and receipt.parent.resolve() == receipt.parent,
            'receipt must be absent with resolved existing parent')
    require(git(EVIDENCE, 'rev-parse', 'HEAD').decode().strip() == PUBLISHED,
            'wrong durable evidence HEAD')
    git(EVIDENCE, 'verify-commit', PUBLISHED)
    require(not git(EVIDENCE, 'status', '--porcelain=v1', '--untracked-files=no'),
            'tracked evidence worktree not clean')
    packet_check(PACKET, 75, 4)
    packet_check(VENDOR, 84, 81)
    collector = PACKET / 'recipes/collect.py'
    require(sha(collector.read_bytes()) == COLLECTOR_SHA, 'wrong admitted collector body')
    api = runpy.run_path(str(collector))  # Definitions only; collector main not invoked.
    records = json.loads((PACKET / 'RECONSTRUCTION.json').read_text())
    retained = json.loads((PACKET / 'RETAINED_REPOSITORIES.json').read_text())
    expected = [str(api['BASE'] / name) for name in api['PARENTS']]
    require(len(expected) == 66 and [r['parent'] for r in records] == expected,
            'exact archived parent list differs')
    admitted_refs = None

    def refs_check(old_rows, current_rows):
        old = dict(row.split(' ', 1) for row in old_rows)
        current = dict(row.split(' ', 1) for row in current_rows)
        require(old.keys() == current.keys(), 'shared ref set changed')
        for ref, oid in old.items():
            if ref in ADVANCES:
                require(current[ref] == PUBLISHED, 'wrong published writer ref')
                subprocess.run(['git', '-C', str(api['CB']), 'merge-base',
                                '--is-ancestor', oid, PUBLISHED], check=True)
            else:
                require(current[ref] == oid, 'unapproved ref change: ' + ref)
        return current_rows

    def incoming_check():
        selected = {Path(n['common_store']) / 'objects'
                    for item in records for n in item['nested']}
        for admin in (api['CB'] / '.git', api['FW'] / '.git'):
            for alternate in admin.rglob('alternates'):
                if alternate.parent.name != 'info' or alternate.parent.parent.name != 'objects':
                    continue
                for line in alternate.read_text().splitlines():
                    target = Path(line)
                    if not target.is_absolute():
                        target = alternate.parent.parent / target
                    require(target.resolve() not in selected,
                            'incoming alternate requires owner: ' + str(alternate))

    def inspect_record(item):
        parent = Path(item['parent'])
        require(parent.is_dir() and parent.resolve() == parent, 'invalid exact parent')
        current, unused = api['inspect'](parent)
        refs_check(item['parent_refs'], current['parent_refs'])
        require(current['parent_refs'] == admitted_refs, 'writer refs changed during retirement')
        expected_item = {k: v for k, v in item.items()
                         if k not in ('parent_refs', 'diff_sha256')}
        require({k: v for k, v in current.items() if k != 'parent_refs'} == expected_item,
                'source/signature/status/nested/ref/object/alias metadata differs')
        difference = git(parent, 'diff', '--binary', 'HEAD')
        require(sha(difference) == item['diff_sha256'] and
                difference == (PACKET / (parent.name + '.diff')).read_bytes(), 'parent diff differs')
        for key, snapshot in api['REPOSITORIES'].items():
            require(snapshot['state'] == retained[key], 'retained repository changed')
        for filename in ('.config', '.config.old'):
            source = parent / filename
            saved = PACKET / 'configs' / parent.name / filename
            require(source.exists() == saved.exists(), 'config presence changed')
            if saved.exists():
                require(not source.is_symlink() and source.is_file() and
                        source.read_bytes() == saved.read_bytes(), 'config bytes changed')

    def cheap_retained_check():
        for key, old in retained.items():
            path = Path(key)
            common = Path(git(path, 'rev-parse', '--path-format=absolute',
                              '--git-common-dir').decode().strip()).resolve()
            shallow = common / 'shallow'
            require(str(common) == old['common_store'] and
                    git(path, 'rev-parse', 'HEAD').decode().strip() == old['head'] and
                    api['refs'](path) == old['refs'] and
                    (shallow.read_text() if shallow.is_file() else '') == old['shallow'],
                    'retained refs/head/common/shallow changed')
            for filename in ('alternates', 'http-alternates'):
                alternate = api['RECOVERY'] / 'objects/info' / filename
                require(not alternate.exists() and not alternate.is_symlink(),
                        'isolated recovery started borrowing objects')

    api['REPOSITORIES'].clear()
    current_refs = api['refs'](api['CB'])
    admitted_refs = refs_check(records[0]['parent_refs'], current_refs)
    # Complete fresh admission of all 66 before any deletion.
    for item in records:
        inspect_record(item)
    incoming_check()
    api['REPOSITORIES'].clear()
    for key, old in retained.items():
        require(api['repository_snapshot'](Path(key))['state'] == old,
                'retained snapshot changed')
    receipt.mkdir(mode=0o755)
    (receipt / 'retirement.status').write_text('1\n')
    (receipt / 'published-head.txt').write_text(PUBLISHED + '\n')
    (receipt / 'admitted-refs.json').write_text(json.dumps(admitted_refs, indent=2) + '\n')
    for index, item in enumerate(records, 1):
        cheap_retained_check()
        inspect_record(item)
        incoming_check()
        command = ['git', '-C', str(api['CB']), 'worktree', 'remove', '--force', item['parent']]
        (receipt / f'remove-{index}.command').write_text(json.dumps(command) + '\n')
        with (receipt / f'remove-{index}.log').open('wb') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        (receipt / f'remove-{index}.status').write_text(str(result.returncode) + '\n')
        require(result.returncode == 0, 'removal failed; preserve partial receipt')
        require(not Path(item['parent']).exists(), 'removed parent still exists')
    require(api['refs'](api['CB']) == admitted_refs, 'shared refs changed')
    api['REPOSITORIES'].clear()
    for key, old in retained.items():
        require(api['repository_snapshot'](Path(key))['state'] == old,
                'retained source/object store changed')
    (receipt / 'retirement.status').write_text('0\n')
    print('PASS removed66 exact archived parents; refs/recovery stores retained; no GC/prune')

if __name__ == '__main__':
    main()
