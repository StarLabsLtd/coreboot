#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Verify finite archived receipts; do not run firmware or write media."""

import hashlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import tarfile

root = Path(__file__).resolve().parent
manifest_paths = set()
for row in (root / "SHA256SUMS").read_text().splitlines():
    digest, name = row.split("  ", 1)
    relative = Path(name)
    assert re.fullmatch(r"[0-9a-f]{64}", digest)
    assert not relative.is_absolute() and ".." not in relative.parts
    assert relative.as_posix() not in manifest_paths
    manifest_paths.add(relative.as_posix())
actual_paths = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()}
assert actual_paths == manifest_paths | {"SHA256SUMS"}
subprocess.run(["sha256sum", "--quiet", "-c", "SHA256SUMS"], cwd=root, check=True)
expected = {}
for row in (root / "source-blobs.tsv").read_text().splitlines():
    archive, commit, member, digest = row.split("\t")
    assert Path(archive).name == archive and archive.endswith(".tar.zst")
    assert not Path(member).is_absolute() and ".." not in Path(member).parts
    assert member.startswith("src/") or member.startswith("tests/")
    assert re.fullmatch(r"[0-9a-f]{40}", commit) and re.fullmatch(r"[0-9a-f]{64}", digest)
    assert member not in expected.get(archive, {})
    expected.setdefault(archive, {})[member] = (commit, digest)
for archive, members in expected.items():
    assert len({commit for commit, _ in members.values()}) == 1
    decoded = subprocess.check_output(["zstd", "-qdc", str(root / "sources" / archive)])
    with tarfile.open(fileobj=io.BytesIO(decoded)) as contents:
        regular = {member.name: member for member in contents.getmembers() if member.isfile()}
        assert len(regular) == sum(member.isfile() for member in contents.getmembers())
        assert set(regular) == set(members)
        for member, (_, digest) in members.items():
            assert hashlib.sha256(contents.extractfile(regular[member]).read()).hexdigest() == digest
assert sum(len(members) for members in expected.values()) == 12

# Optional original-byte parity when reviewing on the author host.
copies = 0
for row in (root / "original-copies.tsv").read_text().splitlines():
    copied, original = row.split("\t")
    if Path(original).is_file():
        assert (root / copied).read_bytes() == Path(original).read_bytes(), copied
        copies += 1

for optimization in (0, 2):
    assert (root / f"raw/request-author/o{optimization}.status").read_text().strip() == "0"
    assert "EXIT=0" in (root / f"raw/request-author/o{optimization}.time").read_text()
assert "EXIT=0" in (root / "raw/request-peer/o0.time").read_text()
assert "Ran 10 tests" in (root / "raw/observer-peer/host.log").read_text()
assert "EXIT=0" in (root / "raw/observer-peer/host.time").read_text()
for name in ("ILVgBW", "DeyJJB"):
    assert (root / f"raw/request-failure-{name}/o0.status").read_text().strip() == "1"

protected = (root / "raw/protected-core/include/cdk2/config.h").read_text()
for field, value in (("PROTECTED_VARIABLE_RUNTIME", 1), ("NATIVE_SYSTEM_FMP", 1),
                     ("NATIVE_QEMU_TEST_FMP", 0), ("QEMU_ACCEPTANCE_PROFILE", 0)):
    assert f"#define CONFIG_CDK2_{field} {value}\n" in protected
assert "#define CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME 0\n" in (
    root / "raw/unprotected-core/include/cdk2/config.h").read_text()
assert (root / "raw/protected-core/outer.status").read_text().strip() == "0"
assert (root / "raw/final/native.status").read_text().strip() == "0"
assert (root / "raw/previous-6flg6D/native.status").read_text().strip() == "1"
assert "identity differs from pristine9" in (root / "raw/previous-6flg6D/native.log").read_text()
wrong_leaf = (root / "raw/previous-6flg6D/run-1/cbmem-table-1.bin").read_bytes()
signature, header_size, _, table_size, _, entry_count = struct.unpack_from("<4sIIIII", wrong_leaf)
assert signature == b"LBIO" and header_size + table_size == len(wrong_leaf)
cursor = header_size
wrong_firmware = []
for _ in range(entry_count):
    assert cursor + 8 <= len(wrong_leaf)
    tag, size = struct.unpack_from("<II", wrong_leaf, cursor)
    assert size >= 8 and cursor + size <= len(wrong_leaf)
    if tag == 0x45:
        assert size == 36
        wrong_firmware.append(struct.unpack_from("<III", wrong_leaf, cursor + 24))
    cursor += size
assert cursor == len(wrong_leaf)
assert wrong_firmware == [(0x01703945, 0x001a0009, 8388608)]

run = root / "raw/final/run-1"
assert json.loads((run / "inputs-before.json").read_text()) == json.loads(
    (run / "inputs-after.json").read_text())
result = json.loads((run / "result.json").read_text())
assert result["failure"] is None and result["expect_no_request"] is True
assert result["qemu_seconds"] < result["deadline_seconds"] == 180
assert result["termination"] == "HOST after bounded disk observation, not guest boot success"
body = (run / "cbmem-live.log").read_bytes()
positions = []
for phase, event, status in (
        (b"CAPSULE_RAM", b"complete", b"SUCCESS \\(0x0000000000000000\\)"),
        (b"PCI_ROOTS", b"begin", b"NOT_STARTED \\(0x8000000000000013\\)"),
        (b"CAPSULE_DISK", b"begin", b"NOT_STARTED \\(0x8000000000000013\\)"),
        (b"CAPSULE_DISK", b"complete", b"SUCCESS \\(0x0000000000000000\\)"),
        (b"OS_HANDOFF", b"begin", b"NOT_STARTED \\(0x8000000000000013\\)")):
    prefix = b"CDK2 | " + phase + b" | " + event + b" | "
    lines = [line for line in body.splitlines() if line.startswith(prefix[:-3])]
    assert len(lines) == 1 and re.fullmatch(re.escape(prefix) + status + rb" \| [0-9]+ us", lines[0])
    positions.append(body.find(lines[0]))
assert positions == sorted(set(positions))
assert b"SystemFmp CHECK transport" not in body and b"SystemFmp SET transport" not in body
assert result["observation"]["phase_result"].encode() in body

absence = root / "raw/absence"
for optimization in (0, 2):
    assert "physically erased/uninitialized" in (absence / f"initial-o{optimization}.log").read_text()
    final = (absence / f"final-o{optimization}.log").read_text()
    assert "records=5 current=5; OsIndications absent; historical_key_recorded=0" in final
    assert "formatted clean FTW" in final
assert "EXIT=0" in (absence / "oracle.time").read_text()
for name in ("source-inputs-after.log", "extracted-after.log"):
    assert all(line.endswith(": OK") for line in (absence / name).read_text().splitlines())
for path in root.rglob("*"):
    if path.is_file():
        # dependencies.raw is textual compiler output, not a disk image.
        assert path.suffix not in (".rom", ".elf", ".cap", ".key", ".o")
        assert path.name not in ("nvme.raw", "pflash.rom", "store.bin", "initial-store.bin", "final-store.bin")
        assert not path.read_bytes().startswith(b"\x7fELF")
print(f"Finite receipt hashes/source bodies12/original copies{copies}/typed phases/request-absence scopes: PASS")
