#!/usr/bin/env python3
"""Root-only exact147 cleanup: retain three archives, trash144 generated/text files."""
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
from urllib.parse import unquote

RECIPE = Path("/home/sean/loose-cdk2-files-retirement-plan.7JufYw")
EVIDENCE = Path("/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001")
RELATIVE = ".site-local/validation/20261004/loose-cdk2-files-retirement"
PACKET = EVIDENCE / RELATIVE
MANIFEST_SHA = "7b1582716c5991cd45edc5b7c5bc7758d6b91554eeff8927280d3ce4793e2e74"
PATH_LIST_SHA = "8cfe9de58ff11029e64dbeb7495b09678dc09e6f5f5b1ca8735ac5d94ffd68e7"
PUBLISHED_BASE = "f3fc8485d69ba9b4ebdac374a4f5b34dbb28a8b8"
ARCHIVES = Path("/home/sean/Documents/cdk2-validation/retained-inputs/loose-cdk2-archives")
TRASH = Path("/home/sean/.local/share/Trash")
EXCLUDED = (Path("/home/sean/cdk2-retired-worktree-cleanup-20261004.sh"),
            Path("/home/sean/cdk2-retired-receipt-cleanup-20261004.sh"))

def require(value, message):
    if not value:
        raise RuntimeError(message)

def sha_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def regular(path):
    require(not path.is_symlink() and stat.S_ISREG(path.stat().st_mode) and
            path.resolve() == path, "not a proper regular file: " + str(path))

def git(*args):
    return subprocess.check_output(["git", "-C", str(EVIDENCE), *args],
                                   stderr=subprocess.STDOUT)

def identity(path):
    regular(path)
    current = path.stat()
    return {"dev": current.st_dev, "inode": current.st_ino,
            "size": current.st_size, "sha256": sha_file(path)}

def tool_state():
    rows = []
    for name in ("python3", "git", "sha256sum", "mv", "gio"):
        alias = shutil.which(name)
        require(alias is not None, "missing utility: " + name)
        actual = Path(alias).resolve()
        require(actual.is_file() and os.access(actual, os.X_OK), "invalid utility")
        rows.append({"name": name, "alias": alias, "actual": str(actual),
                     "sha256": sha_file(actual)})
    return rows

def main():
    require(len(sys.argv) == 3, "usage: retire.py ABSENT_RECEIPT ROOT_VERIFIED_PUSHED_SIGNED_HEAD")
    require(os.environ.get("PYTHONDONTWRITEBYTECODE") == "1", "disable bytecode")
    receipt = Path(sys.argv[1])
    require(re.fullmatch(r"/home/sean/loose-cdk2-files-retirement-final\.[A-Za-z0-9][A-Za-z0-9._-]*",
                         str(receipt)) is not None and not receipt.exists() and
            not receipt.is_symlink(), "receipt must be new exact owned path")
    require(receipt.parent.resolve() == receipt.parent, "invalid receipt parent")
    published = sys.argv[2]
    require(re.fullmatch("[0-9a-f]{40}", published) is not None, "invalid published head")
    require(git("rev-parse", "HEAD").decode().strip() == published, "wrong evidence HEAD")
    git("verify-commit", published)
    subprocess.run(["git", "-C", str(EVIDENCE), "merge-base", "--is-ancestor",
                    PUBLISHED_BASE, published], check=True)
    for ref in ("refs/heads/agent/validation-evidence-20261001",
                "refs/remotes/github/agent/validation-evidence-20261001"):
        require(git("rev-parse", ref).decode().strip() == published,
                "wrong published evidence writer/tracking ref")
    require(not git("status", "--porcelain=v1", "--untracked-files=no"),
            "tracked evidence changed")
    require(not git("status", "--porcelain=v1", "--untracked-files=all", "--", RELATIVE),
            "durable packet changed")
    files = {str(path.relative_to(PACKET)) for path in PACKET.rglob("*") if path.is_file()}
    require(not any(path.is_symlink() for path in PACKET.rglob("*")), "packet symlink")
    tracked = set(filter(None, git("ls-files", "-z", "--", RELATIVE).split(b"\0")))
    require(tracked == {os.fsencode(RELATIVE + "/" + name) for name in files},
            "packet untracked/missing files")
    require(len(files) == 145, "wrong actual packet count")
    for name in files:
        require(git("show", published + ":" + RELATIVE + "/" + name) ==
                (PACKET / name).read_bytes(), "signed packet byte difference")
    manifest = PACKET / "recipes/MANIFEST.tsv"
    require(sha_file(manifest) == MANIFEST_SHA, "wrong signed literal147 manifest")
    rows = list(csv.DictReader(manifest.read_text().splitlines(), delimiter="\t"))
    paths = [row["path"] for row in rows]
    require(len(rows) == 147 and paths == sorted(set(paths)) and
            hashlib.sha256("".join(path + "\n" for path in paths).encode()).hexdigest() ==
            PATH_LIST_SHA, "wrong exact147 set")
    require({kind: sum(row["kind"] == kind for row in rows)
             for kind in ("text", "elf", "archive")} ==
            {"text": 142, "elf": 2, "archive": 3}, "wrong147 classes")
    for ledger in ("ORIGINAL_SHA256SUMS", "RECIPE_SHA256SUMS"):
        subprocess.run(["sha256sum", "--quiet", "-c", str(PACKET / ledger)], check=True)
    subprocess.run(["sha256sum", "--quiet", "-c", "ARCHIVE_SHA256SUMS"], cwd=PACKET, check=True)
    require((PACKET / "collection.status").read_text() == "0\n", "collector failed")
    mappings = [line.split("\t") for line in (PACKET / "FILES_MAP.tsv").read_text().splitlines()]
    require(len(mappings) == 147 and [row[0] for row in mappings] == paths, "wrong map coverage")
    states = {}
    for row, (source, stored, mode) in zip(rows, mappings):
        path = Path(source)
        require(path.parent == Path("/home/sean") and path.name.startswith("cdk2-") and
                path not in EXCLUDED, "outside exact loose-file scope")
        current = identity(path)
        require(current["size"] == int(row["size"]) and current["sha256"] == row["sha256"],
                "original changed: " + source)
        states[source] = current
        if row["kind"] == "text":
            require(mode == "plain" and path.read_bytes() == (PACKET / stored).read_bytes(),
                    "text counterpart changed")
        elif row["kind"] == "elf":
            require((stored, mode) == ("ELF_METADATA.json", "metadata-only-no-payload"),
                    "wrong ELF metadata disposition")
        else:
            require((stored, mode) ==
                    ("ARCHIVE_MOVE_PLAN.tsv", "proposed-local-move-not-executed"),
                    "wrong archive disposition")
    excluded_states = {str(path): identity(path) for path in EXCLUDED}
    plans = list(csv.DictReader((PACKET / "ARCHIVE_MOVE_PLAN.tsv").read_text().splitlines(),
                               delimiter="\t"))
    expected_archives = {row["path"] for row in rows if row["kind"] == "archive"}
    require(len(plans) == 3 and {row["source"] for row in plans} == expected_archives,
            "wrong archive move plan")
    require(ARCHIVES.parent.is_dir() and ARCHIVES.parent.resolve() == ARCHIVES.parent,
            "retained-inputs parent must be proper")
    if ARCHIVES.exists():
        require(ARCHIVES.is_dir() and ARCHIVES.resolve() == ARCHIVES and
                not any(ARCHIVES.iterdir()), "archive target must be new or empty")
    else:
        require(not ARCHIVES.is_symlink(), "archive target alias")
    archive_device = (ARCHIVES if ARCHIVES.exists() else ARCHIVES.parent).stat().st_dev
    for plan in plans:
        require(plan["proposed_target"] == str(ARCHIVES / Path(plan["source"]).name) and
                plan["sha256"] == states[plan["source"]]["sha256"] and
                int(plan["size"]) == states[plan["source"]]["size"] and
                plan["payload_copied"] == plan["move_executed"] == "false",
                "unapproved archive target or metadata")
        require(states[plan["source"]]["dev"] == archive_device,
                "cross-filesystem archive move refused")
        target = Path(plan["proposed_target"])
        require(not target.exists() and not target.is_symlink(), "archive target already exists")
    require(TRASH.is_dir() and TRASH.resolve() == TRASH and
            (TRASH / "info").is_dir() and (TRASH / "files").is_dir() and
            (TRASH / "info").resolve() == TRASH / "info" and
            (TRASH / "files").resolve() == TRASH / "files", "proper existing recoverable trash required")
    before_tools = tool_state()
    own_bytes = {name: (RECIPE / name).read_bytes() for name in ("retire.py", "README.txt")}
    # All147 inputs and durable packet are admitted before any move/trash operation.
    receipt.mkdir(mode=0o755)
    (receipt / "retirement.status").write_text("1\n")
    (receipt / "published-head.txt").write_text(published + "\n")
    (receipt / "original-identities.json").write_text(json.dumps(states, indent=2, sort_keys=True) + "\n")
    (receipt / "tools-before.json").write_text(json.dumps(before_tools, indent=2, sort_keys=True) + "\n")
    if not ARCHIVES.exists():
        ARCHIVES.mkdir(mode=0o755)
    completed = []
    def stage(label, command):
        (receipt / (label + ".command")).write_text(json.dumps(command) + "\n")
        with (receipt / (label + ".log")).open("wb") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        (receipt / (label + ".status")).write_text(str(result.returncode) + "\n")
        require(result.returncode == 0, "operation failed: " + label)
    try:
        for index, plan in enumerate(plans, 1):
            source = Path(plan["source"]); target = Path(plan["proposed_target"])
            require(identity(source) == states[str(source)], "archive source changed before move")
            require(not target.exists() and not target.is_symlink(), "archive target collision")
            stage("move-" + str(index), ["mv", "--update=none-fail", "--no-copy",
                                       "-T", "--", str(source), str(target)])
            require(not source.exists() and not source.is_symlink() and
                    identity(target) == states[str(source)],
                    "archive move did not preserve exact device/inode/size/hash")
            completed.append({"source": str(source), "target": str(target), "disposition": "retained-local-rename",
                              "identity": identity(target)})
            (receipt / "completed.json").write_text(json.dumps(completed, indent=2, sort_keys=True) + "\n")
        for index, row in enumerate((item for item in rows if item["kind"] != "archive"), 1):
            source = Path(row["path"])
            require(identity(source) == states[str(source)], "trash source changed")
            before_info = set((TRASH / "info").iterdir())
            stage("trash-" + str(index), ["gio", "trash", "--", str(source)])
            require(not source.exists() and not source.is_symlink(), "trashed original remains")
            candidates = []
            for info in set((TRASH / "info").iterdir()) - before_info:
                regular(info)
                original = [line[5:] for line in info.read_text().splitlines() if line.startswith("Path=")]
                if len(original) == 1 and unquote(original[0]) == str(source):
                    candidates.append(info)
            require(len(candidates) == 1, "recoverable trash metadata missing/ambiguous")
            info = candidates[0]; payload = TRASH / "files" / info.name.removesuffix(".trashinfo")
            require(identity(payload) == states[str(source)], "recoverable trash payload differs")
            completed.append({"source": str(source), "target": str(payload), "trash_info": str(info),
                              "disposition": "recoverable-trash", "identity": identity(payload)})
            (receipt / "completed.json").write_text(json.dumps(completed, indent=2, sort_keys=True) + "\n")
        require(len(completed) == 147, "partial cleanup")
        for item in completed:
            require(identity(Path(item["target"])) == item["identity"], "retained/trash payload changed")
        for path, current in excluded_states.items():
            require(identity(Path(path)) == current, "excluded active cleanup script changed")
        require(tool_state() == before_tools, "enumerated utility changed")
        for name, data in own_bytes.items():
            require((RECIPE / name).read_bytes() == data, "retirement recipe changed")
        require(git("rev-parse", "HEAD").decode().strip() == published,
                "evidence HEAD changed during retirement")
        subprocess.run(["sha256sum", "--quiet", "-c", "ARCHIVE_SHA256SUMS"], cwd=PACKET, check=True)
        (receipt / "QUALIFICATION.txt").write_text(
            "Exact147 cleanup:3 same-filesystem nonclobber/no-copy archive renames with unchanged inode/size/SHA;\n"
            "142 preserved historical text and2 metadata-only generated ELFs now in recoverable gio trash.\n"
            "No archive duplication, source/worktree/object/ref mutation or unrelated cleanup.\n"
            "Keep retained archives and trash until separate Root policy; no trash-emptying authority.\n")
        (receipt / "retirement.status").write_text("0\n")
        print("PASS retained3 archive renames, recoverable-trash144; excluded active2 unchanged", flush=True)
    except BaseException:
        (receipt / "retirement.status").write_text("1\n")
        raise

if __name__ == "__main__":
    main()
