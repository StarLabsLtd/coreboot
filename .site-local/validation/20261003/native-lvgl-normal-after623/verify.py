#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Verify finite normal UI receipts and replay the original visual prefix."""
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import tarfile
import tempfile
import uuid
from PIL import Image

root = Path(__file__).resolve().parent
digest = lambda body: hashlib.sha256(body).hexdigest()
def safe(name):
    path = PurePosixPath(name)
    assert not path.is_absolute() and '..' not in path.parts and str(path) == name
    return root / name
manifest = {}
for line in (root / 'SHA256SUMS').read_text().splitlines():
    sha, name = line.split('  ', 1)
    assert re.fullmatch('[0-9a-f]{64}', sha) and name not in manifest
    assert safe(name).is_file() and not safe(name).is_symlink()
    assert digest(safe(name).read_bytes()) == sha
    manifest[name] = sha
actual = {str(path.relative_to(root)) for path in root.rglob('*') if path.is_file()}
assert actual == set(manifest) | {'SHA256SUMS'}
rows = set()
for line in (root / 'original-copies.tsv').read_text().splitlines():
    name, original, sha = line.split('\t')
    assert name not in rows and manifest[name] == sha
    rows.add(name)
    if Path(original).exists():
        assert digest(Path(original).read_bytes()) == sha
source_rows = {}
for line in (root / 'sources/identities.tsv').read_text().splitlines():
    commit, name, sha = line.split('\t')
    assert commit == 'cb71f948001ddfcc2b366d645b12a61e08d3ff1a'
    safe(name)
    assert name not in source_rows and re.fullmatch('[0-9a-f]{64}', sha)
    source_rows[name] = sha
archive = subprocess.check_output(['zstd', '-dc', str(root / 'sources/consumer623.tar.zst')])
with tempfile.TemporaryDirectory(prefix='normal-ui-public-verify.') as temporary:
    extracted = Path(temporary)
    members = {}
    with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
        for member in stream:
            safe(member.name.rstrip('/'))
            if member.isdir():
                continue
            assert member.isfile() and member.name not in members
            data = stream.extractfile(member).read()
            members[member.name] = digest(data)
            destination = extracted / member.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
    assert members == source_rows and len(members) == 18
    sys.path.insert(0, str(extracted / 'util/qemu/bin'))
    from qmp_cbmem_console import parse_table, validate_saved_console
    before = (root / 'default-zero/resolved.config').read_bytes()
    after = (root / 'interactive-two/resolved.config').read_bytes()
    old = b'CONFIG_CDK2_BOOT_TIMEOUT=0\n'
    assert before.count(old) == 1
    assert after == before.replace(old, b'CONFIG_CDK2_BOOT_TIMEOUT=2\n')
    for profile, count in (('default-zero', 4), ('interactive-two', 7)):
        run = root / profile / 'ui-run'
        assert json.loads((run / 'inputs-before.json').read_text()) == json.loads((run / 'inputs-after.json').read_text())
        frames = sorted(run.glob('setup-*.ppm'))
        assert len(frames) == count
        for frame in frames:
            with Image.open(frame) as a, Image.open(frame.with_suffix('.png')) as b:
                assert a.size == b.size and a.convert('RGB').tobytes() == b.convert('RGB').tobytes()
        body = validate_saved_console(run)
        assert b'SystemFmp CHECK transport' not in body and b'SystemFmp SET transport' not in body
    run = root / 'interactive-two/ui-run'
    result = json.loads((run / 'result.json').read_text())
    assert result['failure'] is None and result['observer_status'] == result['qemu_status'] == 0
    metadata = json.loads((run / 'cbmem-observer.json').read_text())
    records = parse_table((run / f'cbmem-table-{len(metadata["tables"]) - 1}.bin').read_bytes())
    assert [record[8:] for tag, record in records if tag == 0x45] == [
        uuid.UUID('00112233-4455-6677-8899-aabbccddeeff').bytes_le + struct.pack('<III', 0x001a0009, 0x001a0009, 8388608)]
    assert not any(tag == 0x46 for tag, _ in records)
    oracle = extracted / 'util/qemu/bin/assert-setup-run.py'
    text = oracle.read_text()
    anchor = '\nserial_path = run / "serial.log"\n'
    assert text.count(anchor) == 1
    prefix = text.split(anchor)[0]
    binding = json.loads((root / 'interactive-two/pixel-oracle-source-binding.json').read_text())
    assert digest(oracle.read_bytes()) == binding['full_source_sha256']
    assert digest(prefix.encode()) == binding['verbatim_prefix_sha256']
    sys.argv = [str(oracle), str(run), 'hotkey', 'quiet']
    exec(compile(prefix, str(oracle), 'exec'), {'__name__': '__main__', '__file__': str(oracle)})
print(f'PASS: {len(manifest)} finite hashes, {len(rows)} original copies, 18 source bodies, 11 lossless frames and bounded normal interactive oracle')
