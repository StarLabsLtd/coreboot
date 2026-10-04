#!/usr/bin/env python3
"""Root-only exact seven-tip local LVGL preservation; no source or HEAD writes."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys

BASE = Path('/home/sean/Documents/.cdk2-worktrees')
CANONICAL = Path('/home/sean/Documents/cdk2/3rdparty/lvgl')
CANONICAL_COMMON = Path('/home/sean/Documents/cdk2/.git/modules/3rdparty/lvgl')
PARENTS = [
    ('capsule-ram-window-consumer-after531', 'eb9b2e48d37305cd2d0e4d795ecc2663f4823709'),
    ('q35-auth-provider-metadata-after383', '6afc95e8436708535ddbe6405dcce8d2b07371da')]
TIPS = [
    '64290305189278d5ad023807bb657fb1db905133',
    '9b1977f1e584eddbb71037b3e469ccf2908c7207',
    '691aa2514ea9a6a4f15585bc03cb73e79d669055',
    '5dd2d53bdfa1b525db8700f65b9c387f7877fdb4',
    'c6e3fa30c6cf2996ce86e9083bc567bd95cd8ec2',
    '5f0cd1c3ae98fb40456d83907bbc7f5de398db4e',
    '4d4b2da423adae73ee27886b7f1c060ee7747069']

def require(value, message):
    if not value:
        raise RuntimeError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def git(path, *args):
    return subprocess.check_output(['git', '-c', 'gc.auto=0', '-c', 'maintenance.auto=false',
                                    '-C', str(path), *args], stderr=subprocess.STDOUT)

def refs(path):
    return dict(line.split(' ', 1) for line in
                git(path, 'for-each-ref', '--format=%(refname) %(objectname)').decode().splitlines())

def tracked(path):
    records = []
    for entry in filter(None, git(path, 'ls-files', '--stage', '-z').split(b'\0')):
        attributes, spelling = entry.split(b'\t', 1)
        mode, oid, stage = attributes.decode().split()
        require(stage == '0', 'unmerged source')
        relative = os.fsdecode(spelling)
        actual = path / relative
        if mode == '160000':
            records.append({'path': relative, 'mode': mode, 'gitlink': oid})
        elif mode == '120000':
            require(actual.is_symlink(), 'tracked symlink changed')
            records.append({'path': relative, 'mode': mode, 'target': os.readlink(actual)})
        else:
            require(mode in ('100644', '100755') and actual.is_file() and not actual.is_symlink()
                    and stat.S_ISREG(actual.stat().st_mode), 'tracked source not regular')
            records.append({'path': relative, 'mode': mode, 'sha256': sha(actual.read_bytes())})
    return records

def snapshot(path):
    require(path.is_dir() and path.resolve() == path, 'improper exact source path')
    common = Path(git(path, 'rev-parse', '--path-format=absolute', '--git-common-dir').decode().strip()).resolve()
    alternates = {}
    for filename in ('alternates', 'http-alternates'):
        alternate = common / 'objects/info' / filename
        if alternate.exists() or alternate.is_symlink():
            require(filename == 'alternates' and not alternate.is_symlink() and
                    alternate.is_file() and stat.S_ISREG(alternate.stat().st_mode),
                    'invalid/http alternate refused')
            expected_common = Path('/home/sean/Documents/cdk2/.git/worktrees/'
                                   'capsule-ram-window-consumer-after531/modules/3rdparty/lvgl')
            expected_objects = CANONICAL_COMMON / 'objects'
            raw = alternate.read_bytes()
            require(common == expected_common and expected_objects.is_dir() and
                    expected_objects.resolve() == expected_objects and
                    raw == (str(expected_objects) + '\n').encode(),
                    'foreign/dangling/changed alternate refused')
            alternates[filename] = {'raw': raw.decode(), 'sha256': sha(raw)}
    require(git(path, 'rev-parse', '--is-shallow-repository').strip() == b'false', 'shallow repository refused')
    config = subprocess.run(['git', '-C', str(path), 'config', '--get-regexp',
                             r'^(remote\..*\.promisor|extensions\.partialclone)$'],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    require(config.returncode == 1 and not config.stdout, 'promisor/partial repository refused')
    status = git(path, 'status', '--porcelain=v1', '-z', '--ignored', '--untracked-files=all')
    require(not status, 'dirty or ignored source input refused: ' + str(path))
    metadata = {}
    for filename in ('config', 'shallow', 'FETCH_HEAD'):
        item = common / filename
        require(not item.is_symlink(), 'unexpected Git metadata symlink')
        metadata[filename] = sha(item.read_bytes()) if item.is_file() else None
    raw_refs = refs(path)
    head = git(path, 'rev-parse', 'HEAD').decode().strip()
    objects = {oid: git(path, 'cat-file', '-t', oid).decode().strip()
               for oid in sorted(set(raw_refs.values()) | {head})}
    return {'path': str(path), 'common': str(common), 'head': head, 'refs': raw_refs,
            'raw_types': objects, 'metadata_sha256': metadata, 'tracked': tracked(path),
            'status_hex': status.hex(), 'alternates': alternates}

def reachable():
    return set(git(CANONICAL, 'rev-list', '--all', '--objects', '--no-object-names').decode().splitlines())

def tools():
    rows = []
    for name in ('python3', 'git', 'ssh-keygen', 'sha256sum'):
        alias = shutil.which(name)
        require(alias is not None, 'missing enumerated utility')
        actual = Path(alias).resolve()
        require(actual.is_file() and os.access(actual, os.X_OK), 'invalid utility')
        rows.append({'name': name, 'alias': alias, 'actual': str(actual),
                     'sha256': sha(actual.read_bytes())})
    return rows

def save(receipt, filename, value):
    (receipt / filename).write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')

def main():
    require(len(sys.argv) == 2, 'usage: preserve.py ABSENT_OWNED_RECEIPT')
    require(os.environ.get('PYTHONDONTWRITEBYTECODE') == '1' and
            os.environ.get('GIT_NO_LAZY_FETCH') == '1', 'bytecode/lazy fetch must be disabled')
    receipt = Path(sys.argv[1])
    require(re.fullmatch(r'/home/sean/lvgl-seven-tip-preservation-final\.[A-Za-z0-9][A-Za-z0-9._-]*', str(receipt))
            and not receipt.exists() and not receipt.is_symlink()
            and receipt.parent.resolve() == receipt.parent, 'receipt must be absent exact owned path')
    own = Path(__file__).resolve().parent
    recipes = {name: (own / name).read_bytes() for name in ('preserve.py', 'README.txt')}
    before_tools = tools()
    parents, sources = [], []
    for name, expected_head in PARENTS:
        parent = BASE / name
        require(git(parent, 'rev-parse', 'HEAD').decode().strip() == expected_head, 'wrong signed parent')
        git(parent, 'verify-commit', expected_head)
        require(git(parent, 'for-each-ref', '--points-at', expected_head,
                    '--format=%(refname)', 'refs/heads').strip(), 'signed parent has no retained branch')
        # Parent has ordinary other submodule stores; these are not promotion sources.
        parent_state = {'path': str(parent), 'head': expected_head, 'refs': refs(parent),
                        'status_hex': git(parent, 'status', '--porcelain=v1', '-z', '--ignored',
                                          '--untracked-files=all').hex(), 'tracked': tracked(parent)}
        require(parent_state['status_hex'] == '', 'parent source not clean')
        parents.append(parent_state)
        state = snapshot(parent / '3rdparty/lvgl')
        require(state['head'] == next(row['gitlink'] for row in parent_state['tracked']
                                     if row['path'] == '3rdparty/lvgl'),
                'selected vendor HEAD differs from signed parent gitlink')
        require(state['common'] == '/home/sean/Documents/cdk2/.git/worktrees/' + name + '/modules/3rdparty/lvgl',
                'unexpected selected source store')
        sources.append(state)
    canonical = snapshot(CANONICAL)
    require(canonical['common'] == str(CANONICAL_COMMON) and
            all(state['head'] == canonical['head'] for state in sources), 'wrong pinned canonical source')
    before_reachable = reachable()
    new_refs = {'refs/archive/retired-lvgl-' + oid: oid for oid in TIPS}
    require(not (set(new_refs) & set(canonical['refs'])), 'recovery ref already exists')
    for state in sources:
        missing = set(state['raw_types']) - before_reachable
        require(missing == set(TIPS) and all(state['raw_types'][oid] == 'commit' for oid in TIPS),
                'unexpected missing raw-ref target/type')
    receipt.mkdir(mode=0o755)
    (receipt / 'preservation.status').write_text('1\n')
    save(receipt, 'parents-before.json', parents)
    save(receipt, 'sources-before.json', sources)
    save(receipt, 'canonical-before.json', canonical)
    save(receipt, 'tools-before.json', before_tools)
    save(receipt, 'promotions.json', new_refs)
    for filename, data in recipes.items():
        (receipt / filename).write_bytes(data)
    try:
        source_common = sources[0]['common']
        for index, oid in enumerate(TIPS, 1):
            ref = 'refs/archive/retired-lvgl-' + oid
            command = ['git', '-c', 'gc.auto=0', '-c', 'maintenance.auto=false',
                       '-C', str(CANONICAL), 'fetch', '--no-tags', '--no-recurse-submodules',
                       '--no-write-fetch-head', source_common, oid + ':' + ref]
            save(receipt, f'fetch-{index}.command.json', command)
            with (receipt / f'fetch-{index}.log').open('wb') as output:
                result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT)
            (receipt / f'fetch-{index}.status').write_text(str(result.returncode) + '\n')
            require(result.returncode == 0 and git(CANONICAL, 'rev-parse', ref).decode().strip() == oid
                    and git(CANONICAL, 'cat-file', '-t', oid).strip() == b'commit', 'exact local promotion failed')
        after_sources = [snapshot(Path(state['path'])) for state in sources]
        require(after_sources == sources, 'source HEAD/ref/config/shallow/bytes changed')
        after_parents = []
        for state in parents:
            parent = Path(state['path'])
            after_parents.append({'path': str(parent), 'head': git(parent, 'rev-parse', 'HEAD').decode().strip(),
                                  'refs': refs(parent), 'status_hex': git(parent, 'status', '--porcelain=v1', '-z',
                                                                         '--ignored', '--untracked-files=all').hex(),
                                  'tracked': tracked(parent)})
        require(after_parents == parents, 'signed parent source or refs changed')
        after = snapshot(CANONICAL)
        require(after['refs'] == canonical['refs'] | new_refs, 'unexpected canonical ref mutation')
        unchanged = ('path', 'common', 'head', 'metadata_sha256', 'tracked', 'status_hex')
        require(all(after[key] == canonical[key] for key in unchanged),
                'canonical HEAD/config/shallow/source changed')
        after_reachable = reachable()
        require(before_reachable <= after_reachable, 'existing object reachability lost')
        for state in sources:
            for oid, kind in state['raw_types'].items():
                require(oid in after_reachable and git(CANONICAL, 'cat-file', '-t', oid).decode().strip() == kind,
                        'selected all-raw-ref object not retained')
        save(receipt, 'sources-after.json', after_sources)
        save(receipt, 'parents-after.json', after_parents)
        save(receipt, 'canonical-after.json', after)
        require(tools() == before_tools and all((own / name).read_bytes() == data for name, data in recipes.items()),
                'enumerated utility or recipe changed')
        (receipt / 'QUALIFICATION.txt').write_text(
            'Seven exact raw LVGL commit tips preserved by local non-force fetch under canonical recovery refs.\n'
            'Two clean signed parents/current vendor HEAD/source/config/shallow/oldrefs unchanged; all raw source objects retained.\n'
            'No network completion, GC/prune, source edit, HEAD change, deletion or historical build/test claim.\n')
        (receipt / 'preservation.status').write_text('0\n')
        ledger = []
        for path in sorted(receipt.iterdir()):
            require(path.is_file() and not path.is_symlink(), 'unexpected receipt entry')
            ledger.append(sha(path.read_bytes()) + '  ' + path.name + '\n')
        (receipt / 'RECEIPT_SHA256SUMS').write_text(''.join(ledger))
        subprocess.run(['sha256sum', '--quiet', '-c', 'RECEIPT_SHA256SUMS'], cwd=receipt, check=True)
        print('PASS seven exact local LVGL recovery refs; both source/allrefs and canonical HEAD/config unchanged', flush=True)
    except BaseException:
        (receipt / 'preservation.status').write_text('1\n')
        raise

if __name__ == '__main__':
    main()

