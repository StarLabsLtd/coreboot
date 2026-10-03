#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Portable finite receipts/raw/argv check: no build, VM or authority change."""
import hashlib
from pathlib import Path, PurePosixPath
import re
import tarfile

ROOT = Path(__file__).resolve().parent
COMMITS = {
    'source-619.tar': '1baa1dbbf5050b62a9fa27c2b373af84df9b92ce',
    'source-620.tar': '127c80df85986ca77ce50f3bfcb6fc98c3ac41dc',
    'source-621.tar': 'd6c29549f28cc51d9951db2be965c64c04124ca0',
}
HEAD = COMMITS['source-620.tar']
REASONS = {
    'coreboot-only': 'show_linear_status(&context, CDK2_LVGL_STATUS_PREPARING) == EFI_SUCCESS',
    'begin-error': 'status_calls == 1 && received.message == CDK2_LVGL_STATUS_SELECTING_BOOT',
    'debug-only': 'status_calls == 1 && received.message == CDK2_LVGL_STATUS_SELECTING_BOOT',
    'reseal': 'allocations == saved_allocations',
    'dirty-bgrt': 'BGRT excludes transient status',
    'owner-generation': 'captured UI generation refusal',
    'callback-identity': 'captured UI callback refusal',
}
MODES = [
    'original', 'coreboot-only', 'begin-error',
    'original', 'coreboot-only', 'begin-error', 'debug-only',
    'original', 'coreboot-only', 'begin-error', 'owner-generation', 'callback-identity',
    'original', 'coreboot-only', 'begin-error', 'reseal', 'dirty-bgrt',
    'owner-generation', 'callback-identity',
]


def safe(name):
    path = PurePosixPath(name)
    assert name and not path.is_absolute() and '..' not in path.parts
    assert str(path) == name
    return ROOT / name


def sha(body):
    return hashlib.sha256(body).hexdigest()


manifest = {}
for line in (ROOT / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert re.fullmatch('[0-9a-f]{64}', digest) and name not in manifest
    path = safe(name)
    assert path.is_file() and not path.is_symlink() and sha(path.read_bytes()) == digest
    manifest[name] = digest
assert {str(p.relative_to(ROOT)) for p in ROOT.rglob('*') if p.is_file()} == set(manifest) | {'SHA256SUMS'}
copies = set()
for line in (ROOT / 'original-copies.tsv').read_text().splitlines():
    digest, name, original = line.split('\t')
    assert name not in copies and manifest[name] == digest
    assert Path(original).is_absolute()
    copies.add(name)
    if Path(original).exists():
        assert safe(name).read_bytes() == Path(original).read_bytes()

source_rows = {archive: {} for archive in COMMITS}
for line in (ROOT / 'source/source-blobs.tsv').read_text().splitlines():
    archive, commit, blob, digest, name = line.split('\t')
    safe(name)
    assert archive in COMMITS and COMMITS[archive] == commit
    assert re.fullmatch('[0-9a-f]{40}', blob) and re.fullmatch('[0-9a-f]{64}', digest)
    assert name not in source_rows[archive]
    source_rows[archive][name] = (blob, digest)
archive_bodies = {}
for archive, rows in source_rows.items():
    assert len(rows) == 5
    members = {}
    with tarfile.open(ROOT / 'source' / archive) as stream:
        for member in stream:
            safe(member.name.rstrip('/'))
            if member.isdir():
                continue
            assert member.isfile() and member.name in rows and member.name not in members
            body = stream.extractfile(member).read()
            blob, digest = rows[member.name]
            assert sha(body) == digest
            assert hashlib.sha1(f'blob {len(body)}\0'.encode() + body).hexdigest() == blob
            members[member.name] = body
    assert set(members) == set(rows)
    archive_bodies[archive] = members
for name, body in archive_bodies['source-620.tar'].items():
    if name != 'tests/splash_status_report_test.sh':
        assert archive_bodies['source-621.tar'][name] == body
assert archive_bodies['source-621.tar']['tests/splash_status_report_test.sh'] != archive_bodies['source-620.tar']['tests/splash_status_report_test.sh']


def receipt(variant, profile):
    output = ROOT / 'ab' / f'{variant}-p{profile}'
    assert (output / 'outer.status').read_text().strip() == '0'
    assert (output / 'source-head.txt').read_text().strip() == HEAD
    for name in ['source-after-check.log', 'inputs-after-check.log']:
        lines = (output / name).read_text().splitlines()
        assert lines and all(line.endswith(': OK') for line in lines)
    lines = (output / 'gate.log').read_text().splitlines()
    modes, outcomes, active = [], [], None
    for line in lines:
        mode = re.fullmatch(r'\+ \[ ([a-z-]+) != original \]', line)
        if mode:
            active = mode.group(1)
            modes.append(active)
        elif line == '+ status=134':
            assert active in REASONS
            outcomes.append((active, 134))
        elif line.startswith('+ grep -Fq '):
            assert active in REASONS and line.startswith('+ grep -Fq ' + REASONS[active] + ' ')
    assert modes == MODES
    assert outcomes == [(mode, 134) for mode in MODES if mode != 'original']
    assert sum(line.startswith('+ ASAN_OPTIONS=') and line.endswith('/report') for line in lines) == 19
    assert sum(line.startswith('+ cmp ') and not line.startswith('+ cmp -s ') for line in lines) == 3
    assert lines.count('Splash owner/begin-sentinel/snapshot lifetime coupled source mutants: PASS') == 1
    match = re.fullmatch(r'WALL=([0-9.]+) USER=([0-9.]+) SYS=([0-9.]+) PEAK_KIB=(\d+) EXIT=0',
                         (output / 'gate.time').read_text().strip())
    assert match
    if variant == 'candidate':
        artifacts, = (output / 'tmp').glob('tmp.*')
        original = archive_bodies['source-620.tar']['src/modules/dxe_core/entry.c']
        for mode in ['debug-only', 'owner-generation', 'callback-identity']:
            assert (artifacts / f'{mode}-reversed.c').read_bytes() == original
    return output, float(match.group(1))


def compiler_records(output, support_count):
    artifacts, = (output / 'tmp').glob('tmp.*')
    sources, fixtures = None, 0
    for debug in (0, 1):
        for optimization in (0, 2):
            raw = (artifacts / f'argv-{debug}-{optimization}').read_bytes()
            assert raw.endswith(b'\0\0')
            records = [[item.decode() for item in record.split(b'\0')]
                       for record in raw[:-2].split(b'\0\0')]
            deps = [args for args in records if '-M' in args]
            objects = [args for args in records if '-c' in args]
            links = [args for args in records if '-Wl,--gc-sections' in args]
            expected_links = {(0, 0): 3, (0, 2): 4, (1, 0): 5, (1, 2): 7}[debug, optimization]
            assert len(deps) == 1 and len(objects) == support_count
            assert len(links) == expected_links and len(records) == 1 + support_count + expected_links
            current_sources = [args[args.index('-c') + 1] for args in objects]
            if sources is None:
                sources = current_sources
            assert current_sources == sources
            dependency_sources = [item for item in deps[0] if item.endswith('.c')]
            assert dependency_sources[0].endswith('/tests/splash_status_report_test.c')
            assert dependency_sources[1:] == sources
            # The recorded paths intentionally name the original invocation,
            # not the location of this public packet or omitted object files.
            expected_objects = [args[args.index('-o') + 1] for args in objects]
            for index, name in enumerate(expected_objects, 1):
                assert name.endswith(f'/support-{debug}-{optimization}/support-{index:04d}.o')
            for args in records:
                assert args[0] == 'cc' and '-std=c11' in args
                assert f'-O{optimization}' in args and f'-DCDK2_SPLASH_TEST_DEBUG={debug}' in args
                assert '-fsanitize=address,undefined' in args and '-fno-sanitize-recover=all' in args
                assert '-fno-pie' in args
            for args in objects:
                assert '-no-pie' not in args and '-Wl,--gc-sections' not in args
            for args in links:
                assert '-no-pie' in args
                assert [item for item in args if item.endswith('.o')] == expected_objects
                assert sum(item.endswith('/tests/splash_status_report_test.c') for item in args) == 1
                assert sum(item.startswith('-DCDK2_SPLASH_ENTRY_SOURCE=') for item in args) == 1
                fixtures += 1
            support = artifacts / f'support-{debug}-{optimization}'
            assert (support / 'dependencies.make').is_file()
            names = (support / 'dependencies.txt').read_text().splitlines()
            hashes = [line.split('  ', 1) for line in (support / 'inputs.sha256').read_text().splitlines()]
            assert names and len(names) == len(set(names))
            assert all(re.fullmatch('[0-9a-f]{64}', digest) for digest, _ in hashes)
            assert set(names) <= {name for _, name in hashes}
    assert fixtures == 19


for profile, old, new in [(0, 222.81, 92.40), (1, 295.62, 110.29)]:
    baseline, baseline_time = receipt('baseline', profile)
    candidate, candidate_time = receipt('candidate', profile)
    assert baseline_time == old and candidate_time == new
    assert not (baseline / 'source.patch').read_bytes()
    patch = (candidate / 'source.patch').read_text()
    assert patch.count('diff --git ') == 1 and 'tests/splash_status_report_test.sh' in patch
    assert (baseline / 'resolved.config').read_bytes() == (candidate / 'resolved.config').read_bytes()
    compiler_records(candidate, {0: 30, 1: 44}[profile])
    print(f'P{profile}: four positives/15 assertion134/three inverses/19 links; support {30 if profile == 0 else 44}x4; {old:.2f}s->{new:.2f}s')
for name in ['lvgl/p1/final-authority-scoped-named.time', 'lvgl/final-p0/named.time',
             'lvgl/native-entry-private-return.time', 'lvgl/final-positive.time',
             'lvgl/joined-619/positive.time', 'lvgl/joined-619/native-entry.time',
             'lvgl/admission-registered-handle.time', 'lvgl/compat-admission.time']:
    assert '\tExit status: 0\n' in (ROOT / name).read_text()
entry_lines = archive_bodies['source-620.tar']['src/modules/dxe_core/entry.c'].splitlines(keepends=True)
start, = [index for index, line in enumerate(entry_lines) if line == b'\tui_generation = core.images.next_handle;\n']
assert entry_lines[start - 1] == b'#if CONFIG_CDK2_LINEAR_SETUP_HOTKEY\n'
end = next(index for index in range(start, len(entry_lines))
           if entry_lines[index] == b'\tif (core.runtime_storage == NULL)\n')
block = b''.join(entry_lines[start - 1:end])
assert (ROOT / 'lvgl/admission-original.h').read_bytes() == block
for mode, reason in [('source', 'admission source equality refusal'),
                     ('generation', 'admission first-load generation refusal'),
                     ('transaction', 'admission owned transaction refusal')]:
    assert (ROOT / f'lvgl/admission-{mode}-inverse.h').read_bytes() == block
    for optimization in [0, 2]:
        body = (ROOT / f'lvgl/admission-{mode}-o{optimization}.log').read_text()
        assert reason in body
        assert not re.search('AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:', body)
assert 'Verbatim strict HOST admission block O0/O2/SAN and six exact causes: PASS' in (ROOT / 'lvgl/admission-registered-handle.log').read_text()
assert 'Real compatibility dispatcher release / verbatim HOST admission O0/O2/SAN: PASS' in (ROOT / 'lvgl/compat-admission.log').read_text()
print(f'PASS: {len(manifest)} finite hashes, {len(copies)} original copies, 15 exact archive Git blobs; HOST-only scopes')
