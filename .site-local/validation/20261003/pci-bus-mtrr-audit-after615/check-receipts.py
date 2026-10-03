#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check finite saved audit receipts; do not build or execute a VM."""
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile

root = Path(__file__).resolve().parent
for line in (root / "files.sha256").read_text().splitlines():
    digest, path = line.split("  ", 1)
    assert path.startswith("./") and ".." not in Path(path).parts
    assert hashlib.sha256((root / path).read_bytes()).hexdigest() == digest, path

packed = subprocess.run(
    ["zstd", "-q", "-d", "-c", str(root / "signed-audit-sources.tar.zst")],
    check=True, capture_output=True,
).stdout
expected = {}
for line in (root / "signed-audit-sources.tsv").read_text().splitlines():
    path, blob, digest = line.split("\t")
    assert path not in expected
    expected[path] = (blob, digest)
with tarfile.open(fileobj=io.BytesIO(packed), mode="r:") as archive:
    files = [member for member in archive.getmembers() if member.isfile()]
    assert len(files) == len(expected) == 7
    for member in files:
        assert member.name in expected
        data = archive.extractfile(member).read()
        blob, digest = expected[member.name]
        assert hashlib.sha256(data).hexdigest() == digest
        prefix = f"blob {len(data)}\0".encode()
        assert hashlib.sha1(prefix + data).hexdigest() == blob

fresh = root / "fresh"
failed = root / "failed-auth2-build"
assert (fresh / "outer.status").read_text().strip() == "0"
assert (failed / "outer.status").read_text().strip() == "2"
assert "WALL=117.07 USER=96.28 SYS=31.79" in (fresh / "build.time").read_text()
assert "WALL=10.22 USER=8.05 SYS=3.61 EXIT=0" in (
    root / "original-artifact/time"
).read_text()
assert (fresh / "source-before.sha256").read_bytes() == (
    fresh / "source-after.sha256"
).read_bytes()
assert len((fresh / "source-before.sha256").read_text().splitlines()) == 1600
assert (fresh / "source.patch").read_bytes() == (fresh / "source-after.patch").read_bytes()
assert "expected 0, found 1" in (failed / "build.log").read_text()
assert "direct composition MTRR hostile mutations: PASS" in (
    root / "original-artifact/log"
).read_text()
assert "cdk2 ELF layout: PASS" in (fresh / "build.log").read_text()

def pci_row(path):
    rows = [line.split("|") for line in path.read_text().splitlines()
            if line.endswith("/PciBusDxe.efi")]
    assert len(rows) == 1 and len(rows[0]) == 5
    return rows[0]

new = pci_row(fresh / "direct-mtrr-ownership-inputs")
old = pci_row(failed / "direct-mtrr-ownership-inputs")
saved = pci_row(root / "original-artifact/manifest")
assert new[0] == old[0] == saved[0] == (
    "bafaf3fc91952bd0028a00eb908ddb7d8a3233bcf946da57f1764455e6b79924"
)
assert new[1:4] == saved[1:4] == ["payload", "false-positive", "1"]
assert old[1:4] == ["payload", "-", "0"]
print("Finite PR615 saved audit/source receipts: PASS (no build or VM)")
