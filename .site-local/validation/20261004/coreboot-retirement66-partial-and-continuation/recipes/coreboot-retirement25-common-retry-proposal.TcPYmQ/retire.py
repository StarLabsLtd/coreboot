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
PRIOR_RECEIPT = Path('/home/sean/coreboot-retirement66-final.20261004-r1')
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
    original_rows = (packet / 'ORIGINAL_SHA256SUMS').read_text().splitlines()
    originals = dict(row.split('  ', 1)[::-1] for row in original_rows)
    require(len(originals) == len(original_rows) == map_count, 'original ledger cardinality')
    mapped = (packet / 'FILES_MAP.tsv').read_text().splitlines()
    require(len(mapped) == map_count, 'unexpected original mapping count')
    for row in mapped:
        original, stored, kind = row.split('\t')
        require(kind == 'plain' and not Path(stored).is_absolute() and
                '..' not in Path(stored).parts, 'unexpected mapping')
        source = Path(original)
        require(original in originals and sha((packet / stored).read_bytes()) == originals[original],
                'stored counterpart does not match original ledger')
        if not source.exists() and packet == PACKET:
            archived = json.loads((PACKET / 'RECONSTRUCTION.json').read_text())
            removed = [Path(item['parent']) for item in archived[:41]]
            require(any(parent in source.parents and not parent.exists() for parent in removed),
                    'missing original not in proved removed prefix')
            continue
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
    require((PRIOR_RECEIPT / 'retirement.status').read_text() == '1\n',
            'original partial failure must remain literal1')
    for index, item in enumerate(records[:41], 1):
        require((PRIOR_RECEIPT / f'remove-{index}.status').read_text() == '0\n' and
                not Path(item['parent']).exists(), 'exact first41 removals not proven')
        command = json.loads((PRIOR_RECEIPT / f'remove-{index}.command').read_text())
        require(command == ['git', '-C', str(api['CB']), 'worktree', 'remove', '--force',
                            item['parent']], 'prior removal command differs')
    records = records[41:]
    require(len(records) == 25 and all(Path(item['parent']).is_dir() for item in records),
            'remaining exact25 admission differs')
    admitted_refs = None
    graph_roots = [api['BASE'], Path('/home/sean/Documents/.cdk2-worktrees'),
                   api['CB'], api['FW']]
    # Include registered external worktrees outside these four source roots.
    for repository in (api['CB'], api['FW']):
        for row in git(repository, 'worktree', 'list', '--porcelain').decode().splitlines():
            if row.startswith('worktree '):
                path = Path(row[9:])
                if path.is_dir() and not any(path == root or root in path.parents
                                            for root in graph_roots):
                    graph_roots.append(path)
    # All proper physical Git dirs and all worktree/submodule .git marker files.
    markers = subprocess.check_output(['find', *map(str, graph_roots),
                                       '-name', '.git', '-print', '-prune']).decode().splitlines()
    require(len(markers) == len(set(markers)), 'duplicate physical Git markers')
    graph_before = None
    private_parent_admin = {
        Path(git(Path(item['parent']), 'rev-parse', '--absolute-git-dir').decode().strip()).resolve():
        Path(item['parent']) for item in records}

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

    def graph_snapshot():
        selected = {Path(n['common_store']): Path(item['parent']) / n['path']
                    for item in records for n in item['nested']}
        stores = {api['CB'] / '.git', api['FW'] / '.git'}
        marker_rows = []
        for spelling in markers:
            marker = Path(spelling)
            kind = 'directory'
            if marker.is_symlink():
                require(not any(Path(item['parent']) in marker.parents for item in records),
                        'selected parent has symlink Git marker')
                raw = os.readlink(marker)
                admin = Path(raw)
                if not admin.is_absolute():
                    admin = marker.parent / admin
                admin = admin.resolve()
                kind = 'symlink'
            elif not marker.exists():
                require(any(Path(item['parent']) in marker.parents and
                            not Path(item['parent']).exists() for item in records),
                        'unexpected vanished Git marker')
                continue
            elif marker.is_dir():
                admin = marker.resolve()
                raw = ''
            else:
                raw = marker.read_text()
                require(raw.startswith('gitdir: ') and len(raw.splitlines()) == 1,
                        'malformed Git marker')
                admin = Path(raw[8:].strip())
                if not admin.is_absolute():
                    admin = marker.parent / admin
                admin = admin.resolve()
                kind = 'gitdir-file'
            common = admin
            commondir = admin / 'commondir'
            common_raw = None
            if commondir.exists() or commondir.is_symlink():
                require(commondir.is_file() and not commondir.is_symlink(),
                        'invalid commondir metadata')
                common_raw = commondir.read_text()
                require(len(common_raw.splitlines()) == 1 and common_raw.strip(),
                        'malformed commondir')
                common = Path(common_raw.strip())
                if not common.is_absolute():
                    common = admin / common
                common = common.resolve()
            # Do not resolve away the direct private parent-admin ownership edge.
            for target in {admin, common}:
                if target in selected:
                    require(kind != 'symlink' and marker.parent == selected[target],
                            'foreign private nested Gitdir/common alias')
                for private, owner in private_parent_admin.items():
                    if target == private or private in target.parents:
                        require(kind != 'symlink' and
                                (marker.parent == owner or owner in marker.parents),
                                'foreign selected parent-admin Gitdir/common alias')
            marker_rows.append({'marker': spelling, 'kind': kind, 'raw': raw,
                                'admin': str(admin), 'commondir': common_raw,
                                'target': str(common), 'exists': common.is_dir()})
            if admin.is_dir():
                stores.add(admin)
            if common.is_dir():
                stores.add(common)
            else:
                require(not any(Path(item['parent']) in marker.parents for item in records),
                        'broken marker in selected parent')
        edges = {}
        for store in sorted(stores):
            for folder, children, unused_files in os.walk(store):
                directory = Path(folder)
                if directory.name != 'objects':
                    continue
                for filename in ('alternates', 'http-alternates'):
                    alternate = directory / 'info' / filename
                    if not alternate.exists():
                        continue
                    require(not alternate.is_symlink() and alternate.is_file(),
                            'nonregular alternate')
                    raw = alternate.read_text()
                    targets = []
                    for line in raw.splitlines():
                        target = Path(line)
                        if not target.is_absolute():
                            target = directory / target
                        target = target.resolve()
                        require(target.is_dir(), 'dangling alternate: ' + str(alternate))
                        require(target not in {p / 'objects' for p in selected},
                                'incoming standalone/admin alternate: ' + str(alternate))
                        require(not any(target == admin or admin in target.parents
                                        for admin in private_parent_admin),
                                'incoming selected parent-admin alternate: ' + str(alternate))
                        targets.append(str(target))
                    edges[str(alternate)] = {'raw': raw, 'targets': targets}
                children[:] = []  # Never traverse Git object payloads.
        return {'markers': marker_rows, 'alternates': edges}

    def incoming_check():
        current = graph_snapshot()
        if graph_before is not None:
            # Removed own stores/markers may disappear; survivors must stay byte-exact.
            require(set(current['alternates']) <= set(graph_before['alternates']),
                    'new alternate file appeared')
            for spelling, old in graph_before['alternates'].items():
                if Path(spelling).exists():
                    require(current['alternates'].get(spelling) == old,
                            'alternate contents changed')
                else:
                    require(any(Path(n['common_store']) in Path(spelling).parents and
                                not Path(item['parent']).exists()
                                for item in records for n in item['nested']) or
                            any(admin in Path(spelling).parents and not owner.exists()
                                for admin, owner in private_parent_admin.items()),
                            'unrelated alternate disappeared')
            for marker in current['markers']:
                original = next((x for x in graph_before['markers']
                                 if x['marker'] == marker['marker']), None)
                require(marker == original, 'Git marker changed')
        return current

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
    # Complete fresh admission of all remaining25 before any continuation deletion.
    for item in records:
        inspect_record(item)
    graph_before = incoming_check()
    api['REPOSITORIES'].clear()
    for key, old in retained.items():
        require(api['repository_snapshot'](Path(key))['state'] == old,
                'retained snapshot changed')
    receipt.mkdir(mode=0o755)
    (receipt / 'retirement.status').write_text('1\n')
    (receipt / 'incoming-graph-before.json').write_text(json.dumps(graph_before, indent=2) + '\n')
    (receipt / 'published-head.txt').write_text(PUBLISHED + '\n')
    (receipt / 'admitted-refs.json').write_text(json.dumps(admitted_refs, indent=2) + '\n')
    for index, item in enumerate(records, 42):
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
    (receipt / 'incoming-graph-after.json').write_text(json.dumps(incoming_check(), indent=2) + '\n')
    (receipt / 'retirement.status').write_text('0\n')
    print('PASS continuation removed25 exact archived parents; original partial41 failure remains1; refs/recovery stores retained; no GC/prune')

if __name__ == '__main__':
    main()

