#!/usr/bin/env python3
"""Root-only finite config preservation and generated-output metadata, no deletion."""
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys

PARENTS = [
    "authvar-bootstrap-failure-atomic",
    "authvar-certdb-codec",
    "authvar-media-backend",
    "authvar-mor-probe",
    "authvar-presence-canonical-bootstrap",
    "authvar-presence-lifecycle-close-dma-install-provider",
    "authvar-presence-producer",
    "authvar-presence-publication",
    "authvar-private-binding",
    "authvar-private-provider",
    "authvar-private-trust",
    "authvar-read-view",
    "authvar-service-status",
    "authvar-set-preflight",
    "authvar-shared-fv",
    "authvar-smmstore-backend",
    "authvar-trust-authority",
    "authvar-view",
    "bootmem-aligned-reservations",
    "bootmem-receipt-exact-tag",
    "capsule-anchor-overlap-fix",
    "generic-inventory-workspace",
    "mor-clear-executor",
    "mor-clear-x86-backend",
    "mor-control-clear-transaction",
    "mor-durable-control-clear",
    "mor-executor-inventory-revalidate",
    "mor-executor-window-boundary",
    "mor-grant-close",
    "mor-grant-discard",
    "mor-grant-take",
    "mor-linear-orchestrator",
    "mor-mtl-dma-guard",
    "mor-private-channel-v2",
    "mor-private-smi-channel",
    "mor-probe-ramstage",
    "mor-seal-transport",
    "mtl-authvar-smm-capacity",
    "mtl-early-ecam-guard",
    "mtl-mor-cold-classification",
    "mtl-mor-executor-bindings",
    "mtl-mor-platform-provider",
    "mtl-mor-x86-binding",
    "mtl-presence-boot-classifier",
    "pci-bme-quiesce",
    "q35-authvar-pflash-backend",
    "q35-authvar-pflash-media",
    "q35-mor-composition",
    "q35-mor-e2e-provider",
    "q35-mor-linear-composition",
    "q35-mor-test-adapter",
    "smm-command-registry",
    "spi-volatile-group-fix"
]
BASE = Path('/home/sean/Documents/.coreboot-worktrees')
CANONICAL = Path('/home/sean/Documents/coreboot/payloads/external/SeaBIOS/seabios')
CB = Path('/home/sean/Documents/coreboot')
FW = Path('/home/sean/Documents/cdk2')
CACHE = None

def require(value, message):
    if not value:
        raise RuntimeError(message)

def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args],
                                   stderr=subprocess.STDOUT)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def refs(path):
    return git(path, 'for-each-ref', '--format=%(refname) %(objectname)').decode().splitlines()

def retained():
    global CACHE
    if CACHE is None:
        common = Path(git(CANONICAL, 'rev-parse', '--path-format=absolute',
                          '--git-common-dir').decode().strip()).resolve()
        objects = set(git(CANONICAL, 'rev-list', '--all', '--objects',
                          '--no-object-names').decode().splitlines())
        shallow = common / 'shallow'
        state = {'path': str(CANONICAL), 'common_store': str(common),
                 'head': git(CANONICAL, 'rev-parse', 'HEAD').decode().strip(),
                 'refs': refs(CANONICAL),
                 'shallow': shallow.read_text() if shallow.is_file() else '',
                 'reachable_names_sha256': sha(('\n'.join(sorted(objects)) + '\n').encode())}
        CACHE = (state, objects)
    return CACHE

def inspect(name):
    parent = BASE / name
    source = parent / 'payloads/external/SeaBIOS/seabios'
    require(parent.is_dir() and parent.resolve() == parent, 'invalid exact parent')
    require(source.is_dir() and source.resolve() == source and (source / '.git').is_dir(),
            'not a proper standalone SeaBIOS clone')
    common = Path(git(source, 'rev-parse', '--path-format=absolute',
                      '--git-common-dir').decode().strip()).resolve()
    require(common == source / '.git', 'clone borrows another Git store')
    for filename in ('alternates', 'http-alternates'):
        alternate = common / 'objects/info' / filename
        require(not alternate.exists() and not alternate.is_symlink(), 'borrowed clone objects')
    require(git(source, 'rev-parse', '--is-shallow-repository').strip() == b'false',
            'unexpected incomplete SeaBIOS clone history')
    partial = subprocess.run(['git', '-C', str(source), 'config', '--get-regexp',
                              '^(remote\\..*\\.promisor|extensions\\.partialclone)$'],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    require(partial.returncode == 1 and not partial.stdout, 'unexpected promisor configuration')
    head = git(source, 'rev-parse', 'HEAD').decode().strip()
    rows = refs(source)
    objects = sorted({head} | {row.split(' ', 1)[1] for row in rows})
    state, reachable = retained()
    admitted = []
    for oid in objects:
        require(oid in reachable, 'raw clone ref object not reachable in canonical SeaBIOS: ' + oid)
        source_type = git(source, 'cat-file', '-t', oid).decode().strip()
        require(source_type == git(CANONICAL, 'cat-file', '-t', oid).decode().strip(),
                'raw object type differs')
        admitted.append({'oid': oid, 'type': source_type})
    require(not git(source, 'diff', '--binary', 'HEAD') and
            not git(source, 'diff', '--binary', '--cached'), 'SeaBIOS tracked source changed')
    status = git(source, 'status', '--porcelain=v1', '-z', '--untracked-files=all', '--ignored')
    generated = []
    for row in filter(None, status.split(b'\0')):
        code, spelling = row[:2], os.fsdecode(row[3:])
        require(code == b'!!', 'untracked/modified SeaBIOS source refuses retirement')
        relative = Path(spelling)
        require(not relative.is_absolute() and '..' not in relative.parts, 'invalid ignored path')
        path = source / relative
        require(not path.is_symlink() and path.is_file() and
                stat.S_ISREG(path.stat().st_mode), 'ignored input not regular')
        require(spelling in ('.config', '.config.old') or spelling.startswith('out/') or
                (spelling.startswith('scripts/') and spelling.endswith('.pyc')),
                'unqualified ignored clone input')
        generated.append({'path': spelling, 'size': path.stat().st_size,
                          'sha256': sha(path.read_bytes()),
                          'payload_preserved': spelling in ('.config', '.config.old')})
    worktrees = git(source, 'worktree', 'list', '--porcelain').decode()
    for row in worktrees.splitlines():
        if row.startswith('worktree '):
            require(Path(row[9:]).resolve() in (source, common), 'foreign clone worktree alias')
    parent_head = git(parent, 'rev-parse', 'HEAD').decode().strip()
    signature = git(parent, 'verify-commit', parent_head).decode()
    parent_refs = git(parent, 'for-each-ref', '--points-at', parent_head,
                      '--format=%(refname)', 'refs/heads').decode().splitlines()
    require(parent_refs, 'parent signed HEAD has no retained branch ref')
    return {'parent': str(parent), 'parent_head': parent_head, 'signature': signature,
            'parent_branch_refs': parent_refs,
            'parent_status': git(parent, 'status', '--porcelain=v1', '-z',
                                 '--untracked-files=all', '--ignored').decode(errors='surrogateescape'),
            'parent_removal_authorized': False,
            'clone': str(source), 'common_store': str(common), 'head': head,
            'refs': rows, 'raw_object_admission': admitted, 'ignored_inputs': generated,
            'status': os.fsdecode(status), 'worktrees': worktrees,
            'retained_snapshot_sha256': sha(json.dumps(state, sort_keys=True).encode())}

def incoming(metadata):
    selected = {Path(item['common_store']) / 'objects' for item in metadata}
    owned = {Path(item['common_store']): Path(item['clone']) for item in metadata}
    roots = [BASE, Path('/home/sean/Documents/.cdk2-worktrees'), CB, FW]
    for repository in (CB, FW):
        for row in git(repository, 'worktree', 'list', '--porcelain').decode().splitlines():
            if row.startswith('worktree '):
                path = Path(row[9:])
                if path.is_dir() and not any(path == root or root in path.parents for root in roots):
                    roots.append(path)
    markers = subprocess.check_output(['find', *map(str, roots),
                                       '-name', '.git', '-print', '-prune']).decode().splitlines()
    stores = {CB / '.git', FW / '.git'}
    marker_rows = []
    for spelling in markers:
        marker = Path(spelling)
        if marker.is_symlink():
            raw = os.readlink(marker)
            admin = Path(raw)
            if not admin.is_absolute():
                admin = marker.parent / admin
            admin = admin.resolve()
            kind = 'symlink'
        elif marker.is_dir():
            admin, raw, kind = marker.resolve(), '', 'directory'
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
        common_raw = None
        commondir = admin / 'commondir'
        if commondir.exists() or commondir.is_symlink():
            require(commondir.is_file() and not commondir.is_symlink(), 'invalid commondir')
            common_raw = commondir.read_text()
            require(len(common_raw.splitlines()) == 1 and common_raw.strip(), 'malformed commondir')
            common = Path(common_raw.strip())
            if not common.is_absolute():
                common = admin / common
            common = common.resolve()
        for target in (admin, common):
            if target in owned:
                require(kind != 'symlink' and marker.parent == owned[target],
                        'foreign selected clone Gitdir/common alias')
            if target.is_dir():
                stores.add(target)
        marker_rows.append({'marker': spelling, 'kind': kind, 'raw': raw,
                            'admin': str(admin), 'commondir': common_raw,
                            'common': str(common), 'exists': common.is_dir()})
    edges = {}
    for store in sorted(stores):
        for folder, children, unused in os.walk(store):
            directory = Path(folder)
            if directory.name != 'objects':
                continue
            for name in ('alternates', 'http-alternates'):
                alternate = directory / 'info' / name
                if not alternate.exists():
                    continue
                require(alternate.is_file() and not alternate.is_symlink(), 'invalid alternate')
                raw = alternate.read_text()
                targets = []
                for line in raw.splitlines():
                    target = Path(line)
                    if not target.is_absolute():
                        target = directory / target
                    target = target.resolve()
                    require(target.is_dir(), 'dangling alternate')
                    require(target not in selected, 'incoming alternate owns clone objects')
                    targets.append(str(target))
                edges[str(alternate)] = {'raw': raw, 'targets': targets}
            children[:] = []
    return {'markers': marker_rows, 'alternates': edges}

def main():
    global CACHE
    require(len(sys.argv) == 2, 'usage: collect.py ABSENT_ABSOLUTE_PACKET')
    require(os.environ.get('PYTHONDONTWRITEBYTECODE') == '1', 'disable bytecode')
    destination = Path(sys.argv[1])
    require(destination.is_absolute() and not destination.exists() and not destination.is_symlink()
            and destination.parent.is_dir() and destination.parent.resolve() == destination.parent,
            'packet must be absent under resolved existing parent')
    require(len(PARENTS) == len(set(PARENTS)) == 53, 'exact 53 list differs')
    own = Path(__file__).resolve().parent
    recipes = [(own / name, (own / name).read_bytes()) for name in ('collect.py', 'README.txt')]
    metadata = [inspect(name) for name in PARENTS]
    before = retained()[0]
    graph_before = incoming(metadata)
    configs = []
    for item in metadata:
        for filename in ('.config', '.config.old'):
            source = Path(item['clone']) / filename
            if source.exists():
                require(source.is_file() and not source.is_symlink(), 'config not regular')
                configs.append((source, 'configs/' + Path(item['parent']).name + '/' + filename,
                                source.read_bytes()))
    destination.mkdir(mode=0o755)
    (destination / 'collection.status').write_text('1\n')
    originals = []
    mappings = []
    for source, relative, data in configs + [(source, 'recipes/' + source.name, data)
                                             for source, data in recipes]:
        saved = destination / relative
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_bytes(data)
        require(saved.read_bytes() == data and source.read_bytes() == data, 'config/recipe byte drift')
        originals.append(sha(data) + '  ' + str(source) + '\n')
        mappings.append(str(source) + '\t' + relative + '\tplain\n')
    (destination / 'RECONSTRUCTION.json').write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n')
    (destination / 'RETAINED_SEABIOS.json').write_text(json.dumps(before, indent=2, sort_keys=True) + '\n')
    (destination / 'GIT_GRAPH.json').write_text(json.dumps(graph_before, indent=2, sort_keys=True) + '\n')
    (destination / 'ORIGINAL_SHA256SUMS').write_text(''.join(originals))
    (destination / 'FILES_MAP.tsv').write_text(''.join(mappings))
    (destination / 'QUALIFICATION.txt').write_text(
        '53 standalone SeaBIOS clone reconstruction/config preservation only.\n'
        'All configs stored byte-exact; ignored out/pyc files have size/hash metadata only, NOT payload recovery.\n'
        'Canonical retained HEAD/all raw refs/object types/reachability are required. No Git objects copied.\n'
        'No generated executable/ROM/rawmedia/source payloads copied; no historical build/test pass claimed.\n'
        'No clone or parent removal authorized here. Dirty parent source must remain.\n')
    CACHE = None
    require([inspect(name) for name in PARENTS] == metadata, 'clone/parent metadata changed')
    require(retained()[0] == before, 'retained SeaBIOS changed')
    require(incoming(metadata) == graph_before, 'Git marker/alternate graph changed')
    subprocess.run(['sha256sum', '--quiet', '-c', str(destination / 'ORIGINAL_SHA256SUMS')], check=True)
    try:
        (destination / 'collection.status').write_text('0\n')
        entries = []
        for path in sorted(destination.rglob('*')):
            require(not path.is_symlink(), 'packet symlink')
            if path.is_file():
                entries.append(sha(path.read_bytes()) + '  ' + str(path.relative_to(destination)) + '\n')
        (destination / 'ARCHIVE_SHA256SUMS').write_text(''.join(entries))
        subprocess.run(['sha256sum', '--quiet', '-c', 'ARCHIVE_SHA256SUMS'], cwd=destination, check=True)
        print('PASS clones53 configs=' + str(len(configs)) + '; generated outputs metadata-only; no removals')
    except BaseException:
        (destination / 'collection.status').write_text('1\n')
        raise

if __name__ == '__main__':
    main()
