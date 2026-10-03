#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Finite archival/source checks; not a fresh VM or omitted-input replay."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile
import tempfile

HEX64 = re.compile(r"[0-9a-f]{64}")
HEX40 = re.compile(r"[0-9a-f]{40}")


def safe(value):
    path = PurePosixPath(value)
    if path.is_absolute() or not path.parts or any(part in (".", "..") for part in path.parts) or str(path) != value:
        raise ValueError(f"unsafe path: {value}")
    return path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--originals", action="store_true", help="host-local original-copy comparison")
    parser.add_argument("--git", metavar="REPO", help="host-local signed Git blob comparison")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    manifest = {}
    for row in (root / "files.sha256").read_text().splitlines():
        digest, name = row.split("  ", 1)
        safe(name)
        if not HEX64.fullmatch(digest) or name in manifest or name == "files.sha256":
            raise ValueError("bad/duplicate manifest row")
        manifest[name] = digest
    actual = set()
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"symlink rejected: {path}")
        if path.is_file():
            actual.add(str(path.relative_to(root)))
    if actual != set(manifest) | {"files.sha256"}:
        raise ValueError("finite file closure differs from manifest")
    for name, digest in manifest.items():
        if sha((root / name).read_bytes()) != digest:
            raise ValueError(f"manifest mismatch: {name}")

    copies = set()
    for row in (root / "original-copies.tsv").read_text().splitlines():
        name, original, digest = row.split("\t")
        safe(name)
        if name in copies or manifest.get(name) != digest or not Path(original).is_absolute():
            raise ValueError("bad original-copy binding")
        copies.add(name)
        if args.originals and (Path(original).is_symlink() or sha(Path(original).read_bytes()) != digest):
            raise ValueError(f"original mismatch: {original}")

    archive_members = {}
    rows = set()
    commits = set()
    for row in (root / "source-identities.tsv").read_text().splitlines():
        archive, commit, name, blob, digest = row.split("\t")
        safe(archive); safe(name)
        if (archive, name) in rows or archive not in manifest or not HEX40.fullmatch(commit) or not HEX40.fullmatch(blob) or not HEX64.fullmatch(digest):
            raise ValueError("bad/duplicate source identity")
        rows.add((archive, name)); commits.add(commit)
        if archive not in archive_members:
            with tarfile.open(root / archive) as tar:
                members = {}
                for member in tar.getmembers():
                    if member.isdir():
                        continue
                    safe(member.name)
                    if not member.isfile() or member.name in members:
                        raise ValueError("unsafe/duplicate archive member")
                    members[member.name] = tar.extractfile(member).read()
                archive_members[archive] = members
        data = archive_members[archive][name]
        if sha(data) != digest or hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest() != blob:
            raise ValueError("archived Git blob identity mismatch")
        if args.git and subprocess.check_output(["git", "-C", args.git, "show", f"{commit}:{name}"]) != data:
            raise ValueError("current Git source mismatch")
    for archive, members in archive_members.items():
        if set(members) != {name for a, name in rows if a == archive}:
            raise ValueError("unlisted archive source member")
    records = (root / "source-commits.txt").read_text().splitlines()
    if len(records) != len(commits) or {line.split()[0] for line in records} != commits or any(line.split()[1] != "G" for line in records):
        raise ValueError("signed source record mismatch")
    if args.git:
        for commit in commits:
            subprocess.run(["git", "-C", args.git, "verify-commit", commit], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Run the exact retired-tree archived oracle against original successful
    # evidence. Maps prove saved before/after equality, NOT the current state of
    # 1,402 external inputs omitted from this public packet.
    commit = "019ca1d4926c95613b074d92520a014e496c4647"
    members = archive_members[f"source/{commit}.tar"]
    with tempfile.TemporaryDirectory(prefix="default-zero-packet-oracle-") as directory:
        temporary = Path(directory)
        for name in ("assert-default-zero-setup-run.py", "qmp_cbmem_console.py", "setup_acceptance_contract.py"):
            (temporary / name).write_bytes(members[f"util/qemu/bin/{name}"])
        for run, mode in (("default-zero-no-key-3", "default-zero-no-key"),
                          ("default-zero-queued-hotkey-7", "default-zero-queued-hotkey")):
            subprocess.run([sys.executable, "-B", str(temporary / "assert-default-zero-setup-run.py"),
                            str(root / "native" / run), mode], check=True)
    # Failure records stay failures; never infer firmware failure from a
    # controller's metadata/order/transport error.
    failures = ("default-zero-hotkey", "default-zero-hotkey-2", "default-zero-no-key-2",
                "default-zero-queued-hotkey-4", "default-zero-queued-hotkey-5", "default-zero-queued-hotkey-6")
    for run in failures:
        result = json.loads((root / "native" / run / "result.json").read_bytes())
        if result.get("failure") is None or result.get("observer_status") == 0:
            raise ValueError("historical failure record lost")
    print(f"PASS: {len(manifest)} finite hashes, {len(copies)} original bindings, {len(rows)} Git source blobs; two saved native oracles")


if __name__ == "__main__":
    main()
