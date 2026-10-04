#!/usr/bin/env python3
"""Finite local validation receipts; not a hostile-resealing attestation API."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

if sys.flags.optimize or 'PYTHONOPTIMIZE' in os.environ:
    raise RuntimeError('optimized Python is forbidden for validation receipts')

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()

def git(tree, *args):
    return subprocess.check_output(['git', '-C', str(tree), *args])

def source(tree):
    tree = Path(tree).resolve()
    assert not git(tree, 'status', '--porcelain', '--untracked-files=normal').strip(), tree
    records = []
    for row in git(tree, 'ls-files', '-s', '-z').split(b'\0'):
        if not row:
            continue
        info, name = row.split(b'\t', 1)
        mode, oid, stage = info.decode().split()
        assert stage == '0'
        name = os.fsdecode(name)
        path = tree / name
        if mode == '160000':
            records.append({'path': name, 'gitlink': oid})
        elif mode == '120000':
            assert path.is_symlink()
            records.append({'path': name, 'symlink': os.readlink(path)})
        else:
            assert path.is_file() and not path.is_symlink(), path
            records.append({'path': name, 'sha256': digest(path), 'mode': mode})
    return {'root': str(tree), 'head': git(tree, 'rev-parse', 'HEAD').decode().strip(),
            'files': records}

def tools():
    paths = set()
    for name in ('bash', 'python3', 'git', 'make', 'gcc', 'g++', 'cc', 'iasl',
                 'kconfig-conf', 'jq', 'qemu-system-x86_64', 'swtpm', 'mdir',
                 'mcopy', 'mdel', 'sha256sum', 'cp', 'cmp'):
        path = shutil.which(name)
        assert path, name
        paths.add(Path(path).resolve())
    # Selected native/producer host compiler support and 32-bit runtime.
    for compiler in ('gcc', 'g++', 'cc', 'x86_64-linux-gnu-gcc'):
        binary = shutil.which(compiler)
        assert binary, compiler
        paths.add(Path(binary).resolve())
        for program in ('cc1', 'cc1plus', 'as', 'ld', 'collect2', 'lto-wrapper', 'ar', 'nm'):
            value = subprocess.check_output([binary, '-print-prog-name=' + program], text=True).strip()
            path = Path(value) if '/' in value else Path(shutil.which(value) or value)
            if path.is_file():
                paths.add(path.resolve())
        for flags in ([], ['-m32']):
            for archive in ('libgcc.a', 'liblto_plugin.so'):
                path = Path(subprocess.check_output([binary, *flags,
                    '-print-file-name=' + archive], text=True).strip())
                assert path.is_file(), path
                paths.add(path.resolve())
    for name in ('x86_64-linux-gnu-gcc-ar', 'x86_64-linux-gnu-gcc-nm',
                 'x86_64-linux-gnu-objcopy', 'x86_64-linux-gnu-objdump',
                 'x86_64-linux-gnu-readelf', 'x86_64-linux-gnu-strip'):
        path = shutil.which(name)
        assert path, name
        paths.add(Path(path).resolve())
    return [{'path': str(path), 'sha256': digest(path)} for path in sorted(paths)]

mode, *args = sys.argv[1:]
if mode == 'snapshot':
    print(json.dumps({'sources': [source(path) for path in args], 'tools': tools()},
                     indent=2, sort_keys=True))
elif mode == 'argv':
    print(json.dumps({'argv': args, 'environment': dict(sorted(os.environ.items()))},
                     indent=2, sort_keys=True))
elif mode == 'files':
    print(json.dumps([{'path': str(Path(path).resolve()), 'sha256': digest(Path(path))}
                      for path in args], indent=2, sort_keys=True))
elif mode == 'verify-files':
    for record in json.loads(Path(args[0]).read_text()):
        path = Path(record['path'])
        assert path.is_file() and not path.is_symlink()
        assert digest(path) == record['sha256'], path
    print('bound actual files unchanged')
elif mode == 'media':
    import importlib.util
    fw, run, rom, cbfstool = map(Path, args)
    spec = importlib.util.spec_from_file_location('freezer', fw / 'util/qemu/bin/freeze-tpm-evidence.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    before = (run / 'pflash-before.rom').read_bytes()
    after = (run / 'pflash.rom').read_bytes()
    assert before == rom.read_bytes() and len(before) == len(after)
    regions = module.fmap(cbfstool, rom)
    allowed = [regions['SMMSTORE'], regions['CONSOLE']]
    changed = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
    assert all(any(module.in_region(i, region) for region in allowed) for i in changed)
    assert (run / 'nvme-before.raw').read_bytes() == (run / 'nvme.raw').read_bytes()
    assert (run / 'usb-before.raw').read_bytes() == (fw / 'util/qemu/fixtures/usb.raw').read_bytes()
    assert (run / 'usb-before.raw').read_bytes() == (run / 'usb.raw').read_bytes()
    print(json.dumps({'disk_immutable': True, 'usb_immutable': True, 'coreboot_region_immutable': True,
                      'pflash_changed_bytes': len(changed), 'fmap': regions}, sort_keys=True))
elif mode in ('toolchain', 'native'):
    import importlib.util
    from types import SimpleNamespace
    fw, stage, producer = map(lambda p: Path(p).resolve(), args)
    sys.path.insert(0, str(fw / 'util/qemu/bin'))
    spec = importlib.util.spec_from_file_location('fresh', fw / 'util/qemu/bin/fresh_normal_fwui.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    def regular_digest(path):
        path = Path(path)
        assert path.is_file() and not path.is_symlink(), path
        return digest(path)
    helper = SimpleNamespace(sha=regular_digest)
    if mode == 'toolchain':
        print(json.dumps(module.producer_toolchain(stage, helper), indent=2, sort_keys=True))
    else:
        paths = module.native_build_inputs(stage, fw, producer, helper)
        print(json.dumps([{'path': str(p), 'sha256': regular_digest(p)}
                          for p in sorted(paths)], indent=2, sort_keys=True))
else:
    raise ValueError(mode)
