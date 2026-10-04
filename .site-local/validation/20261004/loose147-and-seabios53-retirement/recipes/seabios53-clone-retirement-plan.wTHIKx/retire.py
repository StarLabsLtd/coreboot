#!/usr/bin/env python3
"""Root-only exact SeaBIOS clone-leaf recovery-trash operation; no parent removal."""
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys
from urllib.parse import unquote

EVIDENCE = Path('/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001')
RELATIVE = '.site-local/validation/20261004/seabios53-config-reconstruction'
PACKET = EVIDENCE / RELATIVE
BASE_HEAD = '296aaee198c6db3c956bff6afb9a73ff092dcc12'
LEDGER_SHA = 'e8d910dcf2399799f3ebffd41ac9a0dd90aa8a42e9317635dd6b3d784a8dcb75'
COLLECTOR_SHA = 'a1334be54e53ce923716dd639076a94679baf1afec3f7cd2c42a28f298563fdf'
TRASH = Path('/home/sean/.local/share/Trash')

def require(value, message):
    if not value:
        raise RuntimeError(message)

def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()

def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], stderr=subprocess.STDOUT)

def directory_identity(path):
    require(path.is_dir() and not path.is_symlink() and path.resolve() == path,
            'not a proper directory: ' + str(path))
    state = path.stat()
    return {'dev': state.st_dev, 'inode': state.st_ino}

def utilities():
    rows = []
    for name in ('python3', 'git', 'ssh-keygen', 'sha256sum', 'gio', 'find'):
        alias = shutil.which(name)
        require(alias is not None, 'missing utility')
        actual = Path(alias).resolve()
        require(actual.is_file() and os.access(actual, os.X_OK), 'invalid utility')
        rows.append({'name': name, 'alias': alias, 'actual': str(actual), 'sha256': sha(actual)})
    return rows

def main():
    require(len(sys.argv) == 3, 'usage: retire.py ABSENT_RECEIPT ROOT_VERIFIED_PUSHED_SIGNED_HEAD')
    require(os.environ.get('PYTHONDONTWRITEBYTECODE') == '1', 'disable bytecode')
    receipt = Path(sys.argv[1])
    require(re.fullmatch(r'/home/sean/seabios53-clone-retirement-final\.[A-Za-z0-9][A-Za-z0-9._-]*', str(receipt))
            and not receipt.exists() and not receipt.is_symlink() and receipt.parent.resolve() == receipt.parent,
            'receipt must be new exact owned path')
    head = sys.argv[2]
    require(re.fullmatch('[0-9a-f]{40}', head) and git(EVIDENCE, 'rev-parse', 'HEAD').decode().strip() == head,
            'wrong published evidence HEAD')
    git(EVIDENCE, 'verify-commit', head)
    subprocess.run(['git', '-C', str(EVIDENCE), 'merge-base', '--is-ancestor', BASE_HEAD, head], check=True)
    for ref in ('refs/heads/agent/validation-evidence-20261001',
                'refs/remotes/github/agent/validation-evidence-20261001'):
        require(git(EVIDENCE, 'rev-parse', ref).decode().strip() == head, 'wrong evidence writer/tracking ref')
    require(not git(EVIDENCE, 'status', '--porcelain=v1', '--untracked-files=no') and
            not git(EVIDENCE, 'status', '--porcelain=v1', '--untracked-files=all', '--', RELATIVE),
            'tracked evidence or required packet changed')
    physical = {str(path.relative_to(PACKET)) for path in PACKET.rglob('*') if path.is_file()}
    require(len(physical) == 116 and not any(path.is_symlink() for path in PACKET.rglob('*')),
            'unexpected packet files')
    tracked = set(filter(None, git(EVIDENCE, 'ls-files', '-z', '--', RELATIVE).split(b'\0')))
    require(tracked == {os.fsencode(RELATIVE + '/' + name) for name in physical}, 'packet missing/untracked files')
    for name in physical:
        require(git(EVIDENCE, 'show', head + ':' + RELATIVE + '/' + name) == (PACKET / name).read_bytes(),
                'signed packet byte difference')
    require(sha(PACKET / 'ARCHIVE_SHA256SUMS') == LEDGER_SHA and
            (PACKET / 'collection.status').read_text() == '0\n', 'wrong qualified packet')
    for ledger in ('ARCHIVE_SHA256SUMS', 'ORIGINAL_SHA256SUMS'):
        subprocess.run(['sha256sum', '--quiet', '-c', str(PACKET / ledger)], cwd=PACKET, check=True)
    collector = PACKET / 'recipes/collect.py'
    require(sha(collector) == COLLECTOR_SHA, 'wrong approved collector definitions')
    api = runpy.run_path(str(collector))  # Definitions only: never invoke collector main.
    records = json.loads((PACKET / 'RECONSTRUCTION.json').read_text())
    retained = json.loads((PACKET / 'RETAINED_SEABIOS.json').read_text())
    require(len(records) == 53 and [Path(row['parent']).name for row in records] == api['PARENTS'],
            'wrong literal53 owners')
    require([api['inspect'](name) for name in api['PARENTS']] == records and api['retained']()[0] == retained,
            'source/config/raw-reference/generated metadata or retained snapshot changed')
    graph = api['incoming'](records)
    # Current selected-clone alias/borrowing guards are authoritative. Unrelated
    # reviewed CDK2 leaf retirements may have removed historical census markers.
    directory_identity(TRASH)
    directory_identity(TRASH / 'files')
    directory_identity(TRASH / 'info')
    identities = {row['clone']: directory_identity(Path(row['clone'])) for row in records}
    require(all(state['dev'] == (TRASH / 'files').stat().st_dev for state in identities.values()),
            'cross-filesystem clone trash refused')
    parent_diffs = {row['parent']: [git(Path(row['parent']), 'diff', '--binary', 'HEAD'),
                                   git(Path(row['parent']), 'diff', '--binary', '--cached')]
                    for row in records}
    own = Path(__file__).resolve().parent
    own_bytes = {name: (own / name).read_bytes() for name in ('retire.py', 'README.txt')}
    tools = utilities()
    completed = []

    def graph_check():
        current = api['incoming'](records)
        removed = [Path(row['source']) for row in completed]
        expected = {row['marker']: row for row in graph['markers']
                    if not any(source in Path(row['marker']).parents for source in removed)}
        require({row['marker']: row for row in current['markers']} == expected and
                current['alternates'] == graph['alternates'], 'surviving graph changed or foreign dependency appeared')
        return current

    receipt.mkdir(mode=0o755)
    (receipt / 'retirement.status').write_text('1\n')
    (receipt / 'published-head.txt').write_text(head + '\n')
    (receipt / 'original-identities.json').write_text(json.dumps(identities, indent=2, sort_keys=True) + '\n')
    (receipt / 'tools-before.json').write_text(json.dumps(tools, indent=2, sort_keys=True) + '\n')
    (receipt / 'incoming-graph-before.json').write_text(json.dumps(graph, indent=2, sort_keys=True) + '\n')
    try:
        for index, row in enumerate(records, 1):
            source = Path(row['clone'])
            require(api['inspect'](Path(row['parent']).name) == row and directory_identity(source) == identities[str(source)],
                    'exact clone changed before trash')
            graph_check()
            api['retained'].__globals__['CACHE'] = None
            require(api['retained']()[0] == retained, 'retained SeaBIOS snapshot changed')
            before_info = set((TRASH / 'info').iterdir())
            command = ['gio', 'trash', '--', str(source)]
            (receipt / f'trash-{index}.command').write_text(json.dumps(command) + '\n')
            with (receipt / f'trash-{index}.log').open('wb') as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
            (receipt / f'trash-{index}.status').write_text(str(result.returncode) + '\n')
            require(result.returncode == 0 and not source.exists() and not source.is_symlink(), 'clone trash failed')
            candidates = []
            for info in set((TRASH / 'info').iterdir()) - before_info:
                require(info.is_file() and not info.is_symlink(), 'invalid trash metadata')
                original = [line[5:] for line in info.read_text().splitlines() if line.startswith('Path=')]
                if len(original) == 1 and unquote(original[0]) == str(source):
                    candidates.append(info)
            require(len(candidates) == 1, 'recoverable clone trash metadata missing/ambiguous')
            info = candidates[0]
            target = TRASH / 'files' / info.name.removesuffix('.trashinfo')
            require(directory_identity(target) == identities[str(source)], 'clone directory inode/device not preserved')
            require(git(target, 'rev-parse', 'HEAD').decode().strip() == row['head'] and api['refs'](target) == row['refs'],
                    'trashed clone HEAD/raw refs differ')
            for filename in ('.config', '.config.old'):
                require((target / filename).read_bytes() ==
                        (PACKET / 'configs' / Path(row['parent']).name / filename).read_bytes(), 'trashed config differs')
            completed.append({'source': str(source), 'target': str(target), 'trash_info': str(info),
                              'identity': directory_identity(target), 'disposition': 'recoverable-clone-trash'})
            (receipt / 'completed.json').write_text(json.dumps(completed, indent=2, sort_keys=True) + '\n')
        require(len(completed) == 53, 'partial clone retirement')
        for row in records:
            parent = Path(row['parent'])
            require(parent.is_dir() and [git(parent, 'diff', '--binary', 'HEAD'),
                                        git(parent, 'diff', '--binary', '--cached')] == parent_diffs[str(parent)],
                    'parent tracked changes changed or parent removed')
        api['retained'].__globals__['CACHE'] = None
        require(api['retained']()[0] == retained, 'final retained canonical snapshot changed')
        final_graph = graph_check()
        (receipt / 'incoming-graph-after.json').write_text(json.dumps(final_graph, indent=2, sort_keys=True) + '\n')
        require(utilities() == tools and all((own / name).read_bytes() == data for name, data in own_bytes.items()),
                'enumerated utilities or recipe changed')
        require(git(EVIDENCE, 'rev-parse', 'HEAD').decode().strip() == head, 'evidence HEAD changed')
        subprocess.run(['sha256sum', '--quiet', '-c', 'ARCHIVE_SHA256SUMS'], cwd=PACKET, check=True)
        (receipt / 'QUALIFICATION.txt').write_text(
            'Exact53 SeaBIOS clone leaves moved to recoverable same-filesystem gio trash; parents retained.\n'
            'Directory inode/device, Trashinfo original path, HEAD/raw refs and configs retained.\n'
            'Signed config/raw-reference packet plus fresh approved source/graph admission; no historical build/HOST claim.\n'
            'Generated outputs are recoverable in trash, not represented as payloads in evidence Git.\n'
            'Original config paths intentionally absent only after recorded successful exact-leaf commands.\n'
            'No parent removal, canonical/ref/object pruning, source edit or trash-emptying authority.\n')
        (receipt / 'retirement.status').write_text('0\n')
        print('PASS recoverable-trash53 exact clone leaves; parent changes and canonical refs retained', flush=True)
    except BaseException:
        (receipt / 'retirement.status').write_text('1\n')
        raise

if __name__ == '__main__':
    main()
