#!/usr/bin/env python3
"""Root-only finite preservation proposal for 147 historical loose files."""
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys

RECIPE = Path("/home/sean/loose-cdk2-files-retirement-retry-collector.Kst9uj")
DESTINATION = Path("/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/loose-cdk2-files-retirement")
ARCHIVE_TARGET = Path("/home/sean/Documents/cdk2-validation/retained-inputs/loose-cdk2-archives")
MANIFEST_SHA = "7b1582716c5991cd45edc5b7c5bc7758d6b91554eeff8927280d3ce4793e2e74"
PATH_LIST_SHA = "8cfe9de58ff11029e64dbeb7495b09678dc09e6f5f5b1ca8735ac5d94ffd68e7"
EXCLUDED = {"cdk2-retired-worktree-cleanup-20261004.sh",
            "cdk2-retired-receipt-cleanup-20261004.sh"}
PRIVATE_KEY = re.compile(rb"-----BEGIN (?:[A-Z0-9 ]+ )?PRIVATE KEY-----")
TOOLS = ("python3", "sha256sum", "file", "readelf", "gzip", "cmp")

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def sha_bytes(data):
    return hashlib.sha256(data).hexdigest()

def sha_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def regular(path):
    require(not path.is_symlink() and stat.S_ISREG(path.stat().st_mode),
            "not an original regular file: " + str(path))
    require(path.resolve() == path, "aliased original path: " + str(path))

def command(*args):
    return subprocess.check_output(list(args), stderr=subprocess.STDOUT)

def tools():
    rows = []
    for name in TOOLS:
        alias = shutil.which(name)
        require(alias is not None, "missing utility: " + name)
        actual = Path(alias).resolve()
        require(actual.is_file() and os.access(actual, os.X_OK),
                "not an executable utility: " + str(actual))
        rows.append({"name": name, "alias": alias, "actual": str(actual),
                     "sha256": sha_file(actual)})
    return rows

def main():
    require(len(sys.argv) == 1, "usage: python3 collect.py (fixed absent destination)")
    require(RECIPE.resolve() == RECIPE, "recipe directory must be proper")
    recipe_bytes = {}
    for name in ("collect.py", "MANIFEST.tsv", "README.txt"):
        source = RECIPE / name
        regular(source)
        recipe_bytes[name] = source.read_bytes()
    require(sha_bytes(recipe_bytes["MANIFEST.tsv"]) == MANIFEST_SHA,
            "literal manifest hash changed")
    rows = list(csv.DictReader(recipe_bytes["MANIFEST.tsv"].decode("utf-8").splitlines(),
                               delimiter="\t"))
    require(len(rows) == 147, "exact147 manifest count required")
    require({kind: sum(row["kind"] == kind for row in rows)
             for kind in ("text", "elf", "archive")} ==
            {"text": 142, "elf": 2, "archive": 3}, "manifest classes changed")
    paths = [row["path"] for row in rows]
    require(paths == sorted(set(paths)), "manifest must be unique and sorted")
    require(sha_bytes(("".join(path + "\n" for path in paths)).encode()) == PATH_LIST_SHA,
            "exact147 path set changed")
    require(not DESTINATION.exists() and not DESTINATION.is_symlink(),
            "packet destination must be absent")
    require(DESTINATION.parent.is_dir() and DESTINATION.parent.resolve() == DESTINATION.parent,
            "packet parent must be existing and proper")
    require(ARCHIVE_TARGET.parent.is_dir() and
            ARCHIVE_TARGET.parent.resolve() == ARCHIVE_TARGET.parent,
            "retained-inputs parent must be existing and proper")
    before_tools = tools()
    text_bytes = {}
    elf_metadata = []
    archive_plan = []
    for row in rows:
        source = Path(row["path"])
        require(source.parent == Path("/home/sean") and source.name.startswith("cdk2-") and
                source.name not in EXCLUDED, "outside finite loose-file scope")
        regular(source)
        require(re.fullmatch("[0-9a-f]{64}", row["sha256"]) is not None,
                "invalid manifest SHA")
        require(source.stat().st_size == int(row["size"]) and
                sha_file(source) == row["sha256"], "original bytes changed: " + str(source))
        if row["kind"] == "text":
            data = source.read_bytes()
            mime = command("file", "--brief", "--mime-type", "--", str(source)).decode().strip()
            require(mime in ("text/plain", "text/x-diff", "inode/x-empty"),
                    "nontext input refused: " + str(source) + " " + mime)
            require(not PRIVATE_KEY.search(data), "private-key header refused: " + str(source))
            text_bytes[str(source)] = data
        elif row["kind"] == "elf":
            with source.open("rb") as stream:
                require(stream.read(4) == b"\x7fELF", "expected historical ELF metadata input")
            elf_metadata.append({"source": str(source), "size": int(row["size"]),
                                 "sha256": row["sha256"], "payload_stored": False,
                                 "file_description": command("file", "--brief", "--", str(source)).decode(),
                                 "readelf_headers": command("readelf", "--wide", "--file-header",
                                                           "--program-headers", "--", str(source)).decode()})
        else:
            with source.open("rb") as stream:
                require(stream.read(4) == b"\x28\xb5\x2f\xfd", "expected historical zstd archive")
            target = ARCHIVE_TARGET / source.name
            require(not target.exists() and not target.is_symlink(),
                    "proposed archive move target already exists")
            archive_plan.append({"source": str(source), "proposed_target": str(target),
                                 "size": int(row["size"]), "sha256": row["sha256"],
                                 "payload_copied": False, "move_executed": False})
    # Admission completes before packet creation. No original file is ever written.
    DESTINATION.mkdir(mode=0o755)
    (DESTINATION / "collection.status").write_text("1\n")
    (DESTINATION / "blobs").mkdir()
    (DESTINATION / "recipes").mkdir()
    mappings = []
    unique = {}
    for row in rows:
        source = Path(row["path"])
        if row["kind"] == "text":
            digest = row["sha256"]
            data = text_bytes[str(source)]
            if digest not in unique:
                compressed = len(data) > 1024 * 1024
                relative = "blobs/" + digest + (".txt.gz" if compressed else ".txt")
                stored = DESTINATION / relative
                stored.write_bytes(command("gzip", "-n", "-c", "--", str(source))
                                   if compressed else data)
                unique[digest] = (relative, compressed)
            relative, compressed = unique[digest]
            stored = DESTINATION / relative
            if compressed:
                require(command("gzip", "-d", "-c", "--", str(stored)) == data,
                        "gzip counterpart differs: " + str(source))
            else:
                subprocess.run(["cmp", "--", str(source), str(stored)], check=True)
            mappings.append((str(source), relative, "gzip" if compressed else "plain"))
        elif row["kind"] == "elf":
            mappings.append((str(source), "ELF_METADATA.json", "metadata-only-no-payload"))
        else:
            mappings.append((str(source), "ARCHIVE_MOVE_PLAN.tsv", "proposed-local-move-not-executed"))
    for name, data in recipe_bytes.items():
        (DESTINATION / "recipes" / name).write_bytes(data)
        require((DESTINATION / "recipes" / name).read_bytes() == data, "recipe copy differs")
    (DESTINATION / "ELF_METADATA.json").write_text(json.dumps(elf_metadata, indent=2, sort_keys=True) + "\n")
    (DESTINATION / "ARCHIVE_MOVE_PLAN.tsv").write_text(
        "source\tproposed_target\tsize\tsha256\tpayload_copied\tmove_executed\n" +
        "".join("\t".join((row["source"], row["proposed_target"], str(row["size"]),
                            row["sha256"], "false", "false")) + "\n"
                for row in archive_plan))
    (DESTINATION / "FILES_MAP.tsv").write_text(
        "".join("\t".join(row) + "\n" for row in mappings))
    (DESTINATION / "ORIGINAL_SHA256SUMS").write_text(
        "".join(row["sha256"] + "  " + row["path"] + "\n" for row in rows))
    (DESTINATION / "RECIPE_SHA256SUMS").write_text(
        "".join(sha_bytes(data) + "  " + str(RECIPE / name) + "\n"
                for name, data in recipe_bytes.items()))
    (DESTINATION / "TOOLS.json").write_text(json.dumps(before_tools, indent=2, sort_keys=True) + "\n")
    (DESTINATION / "QUALIFICATION.txt").write_text(
        "Historical loose-file preservation only; no historical test/runtime success claim.\n"
        "Exact147 original paths:142 text bytes fully mapped to deduplicated lossless blobs;\n"
        "2 generated HOST ELFs have static headers/hash/size only; their executable bytes are NOT archived.\n"
        "3 existing local tar.zst archives have hash/size and a future local MOVE PLAN only;\n"
        "archive payloads are NOT copied, opened, moved, extracted or published by this collector.\n"
        "No original file, source tree, object store, ref, active recipe or unrelated data is changed.\n"
        "No deletion/move/Git write authority is supplied by collector success.\n"
        "Future retirement requires independent packet audit, durable evidence push and separate\n"
        "Root-owned exact retirement plan; archives must first be retained locally without duplicate copies.\n"
        "Utility evidence is finite enumeration, not complete interpreter/runtime attestation.\n")
    for row in rows:
        source = Path(row["path"])
        regular(source)
        require(source.stat().st_size == int(row["size"]) and
                sha_file(source) == row["sha256"], "original changed during collection: " + str(source))
    for name, data in recipe_bytes.items():
        require((RECIPE / name).read_bytes() == data, "recipe changed during collection")
    require(tools() == before_tools, "enumerated tool bytes/resolution changed")
    subprocess.run(["sha256sum", "--quiet", "-c", str(DESTINATION / "ORIGINAL_SHA256SUMS")], check=True)
    subprocess.run(["sha256sum", "--quiet", "-c", str(DESTINATION / "RECIPE_SHA256SUMS")], check=True)
    try:
        (DESTINATION / "collection.status").write_text("0\n")
        stored_rows = []
        for stored in sorted(DESTINATION.rglob("*")):
            require(not stored.is_symlink(), "packet symlink refused")
            if stored.is_file():
                stored_rows.append(sha_file(stored) + "  " + str(stored.relative_to(DESTINATION)) + "\n")
        (DESTINATION / "ARCHIVE_SHA256SUMS").write_text("".join(stored_rows))
        subprocess.run(["sha256sum", "--quiet", "-c", "ARCHIVE_SHA256SUMS"],
                       cwd=DESTINATION, check=True)
        print("PASS originals147 text142 unique_text_blobs=" + str(len(unique)) +
              " ELF_metadata2 local_archive_move_plan3; no moves/deletes executed")
    except BaseException:
        (DESTINATION / "collection.status").write_text("1\n")
        raise

if __name__ == "__main__":
    main()
