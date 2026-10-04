#!/usr/bin/env python3
"""Finite retired coreboot config and reconstruction metadata collector."""
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys

PARENTS = [
    "authvar-general-policy-capability",
    "authvar-presence-authority",
    "authvar-presence-final-publication",
    "authvar-presence-handoff",
    "authvar-presence-lifecycle-close-dma-authority-integration",
    "authvar-presence-lifecycle-close-dma-receipt",
    "authvar-presence-lifecycle-close-dram-authority",
    "authvar-presence-lifecycle-close-endpoint",
    "authvar-presence-lifecycle-close-owner",
    "authvar-presence-lifecycle-close-transport",
    "authvar-presence-lifecycle-closed-s3-restore",
    "authvar-presence-lifecycle-composition",
    "authvar-presence-mailbox-scrub",
    "authvar-presence-restrict",
    "authvar-presence-s3-rearm",
    "authvar-presence-terminal-closure",
    "authvar-presence-transaction",
    "authvar-smm-bootstrap",
    "bootmem-dram-provenance",
    "capsule-delivery-policy-after-composition",
    "capsule-mm-read-admission-after-predicate",
    "capsule-public-trust-package-after-buffers",
    "capsule-ram-handoff",
    "capsule-ram-window-after-dma-tables",
    "composition-harness-baseline",
    "fmp-auth-initial-version-after-provider",
    "fmp-auth-initial-version-after382",
    "fmp-authvar-migration",
    "fmp-boot-owner",
    "fmp-owner-authvar",
    "mor-completion-seal",
    "mor-mtl-inventory",
    "mtl-capsule-platform-prereq",
    "mtl-mor-live-inventory",
    "mtl-smm-requester-role-authority",
    "q35-capsule-auth-policy-after-trust",
    "q35-capsule-broker-owner-after-retention",
    "q35-capsule-dma-loader-after-auth",
    "q35-capsule-ram-dma-after379",
    "q35-exclusive-e8-dma-scope-after385",
    "q35-fresh-root-inventory-after383",
    "q35-pflash-erased-padding-after384",
    "q35-vtd-drain-after-auth",
    "s3-domain-dram-exact-containment",
    "smm-invocation-adapter-provider",
    "smm-invocation-adapter-route",
    "smm-invocation-adapter-spans",
    "smm-invocation-boot-epoch",
    "smm-invocation-composition",
    "smm-invocation-entry-adapter",
    "smm-invocation-epoch",
    "smm-invocation-intel-cause",
    "smm-invocation-loader-composition",
    "smm-invocation-logical-value",
    "smm-invocation-mtl-loader-instance",
    "smm-invocation-runtime-view",
    "smm-invocation-save-state-geometry",
    "smm-invocation-strong-completion",
    "smm-invocation-tuple-sender",
    "smm-invocation-tuple-trigger",
    "smm-presence-arm-adapter",
    "smm-presence-live-session",
    "smm-presence-platform-route",
    "smm-presence-route-session",
    "smmstore-read-region-stacked",
    "vtd-translation-semantic-verifier"
]
BASE = Path("/home/sean/Documents/.coreboot-worktrees")
CB = Path("/home/sean/Documents/coreboot")
FW = Path("/home/sean/Documents/cdk2")
RECOVERY = Path("/home/sean/Documents/cdk2-validation/retained-inputs/vboot-dd38-recovery.git")
SHALLOW_TIP = "dd38e912b39166975ed13e46602d8a5b2a6e85f4"
REPOSITORIES = {}

def fail(message):
    raise RuntimeError(message)

def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args],
                                   stderr=subprocess.STDOUT)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def canonical(display):
    prefix = "payloads/external/cdk2/cdk2"
    if display == prefix:
        return FW
    if display.startswith(prefix + "/"):
        return FW / display[len(prefix) + 1:]
    return CB / display

def refs(path):
    rows = git(path, "for-each-ref", "--format=%(refname) %(objectname)")
    return rows.decode().splitlines()

def repository_snapshot(path):
    spelling = str(path)
    if spelling not in REPOSITORIES:
        ref_rows = refs(path)
        reachable = set(git(path, "rev-list", "--all", "--objects",
                            "--no-object-names").decode().splitlines())
        common = Path(git(path, "rev-parse", "--path-format=absolute",
                          "--git-common-dir").decode().strip()).resolve()
        shallow = common / "shallow"
        state = {"repository": spelling, "common_store": str(common),
                 "head": git(path, "rev-parse", "HEAD").decode().strip(),
                 "refs": ref_rows,
                 "shallow": shallow.read_text() if shallow.is_file() else "",
                 "reachable_object_names_sha256": digest(
                     ("\n".join(sorted(reachable)) + "\n").encode())}
        REPOSITORIES[spelling] = {"state": state, "reachable": reachable, "types": {}}
    return REPOSITORIES[spelling]

def inspect(parent):
    head = git(parent, "rev-parse", "HEAD").decode().strip()
    signature = git(parent, "verify-commit", head).decode()
    branch_refs = git(parent, "for-each-ref", "--points-at", head,
                      "--format=%(refname)", "refs/heads").decode().splitlines()
    if not branch_refs:
        fail(f"{parent}: no exact retained parent branch")
    raw_status = git(parent, "status", "--porcelain=v1", "-z",
                     "--untracked-files=all", "--ignored")
    for row in raw_status.split(b"\0"):
        if not row:
            continue
        status, name = row[:2], os.fsdecode(row[3:])
        allowed_config = name in (".config", ".config.old")
        allowed_cache = name.endswith(".pyc") and name.startswith(("tests/", "util/"))
        tree = git(parent, "ls-files", "--stage", "--", name)
        gitlink = tree.startswith(b"160000 ")
        if not (allowed_config or allowed_cache or (status == b" M" and gitlink)):
            fail(f"{parent}: unqualified source/status {row!r}")
    paths = git(parent, "submodule", "foreach", "--quiet", "--recursive",
                'printf "%s\\n" "$displaypath"').decode().splitlines()
    nested = []
    for display in paths:
        source = parent / display
        if git(source, "status", "--porcelain=v1", "--untracked-files=all", "--ignored"):
            fail(f"{source}: dirty or ignored nested inputs require separate inventory")
        current = git(source, "rev-parse", "HEAD").decode().strip()
        common = Path(git(source, "rev-parse", "--path-format=absolute",
                          "--git-common-dir").decode().strip()).resolve()
        target = canonical(display)
        all_refs = refs(source)
        required = {current}
        required.update(row.split(" ", 1)[1] for row in all_refs)
        availability = []
        for oid in sorted(required):
            admitted = target
            history = "canonical retained repository"
            if display == "3rdparty/vboot" and oid == SHALLOW_TIP:
                admitted = RECOVERY
                if not admitted.is_dir() or admitted.resolve() != admitted:
                    fail("missing proper isolated vboot recovery store")
                if git(admitted, "rev-parse", "--is-bare-repository").strip() != b"true":
                    fail("vboot recovery store must be bare")
                if git(admitted, "rev-parse", "--is-shallow-repository").strip() != b"true":
                    fail("vboot recovery must retain declared incomplete history")
                if git(target, "rev-parse", "--is-shallow-repository").strip() != b"false":
                    fail("canonical vboot must remain nonshallow")
                expected_ref = "refs/archive/retired-vendor-" + oid
                if git(admitted, "rev-parse", expected_ref).decode().strip() != oid:
                    fail("wrong exact shallow-tip recovery ref")
                if oid not in (admitted / "shallow").read_text().splitlines():
                    fail("vboot tip must remain a declared shallow boundary")
                for filename in ("alternates", "http-alternates"):
                    alternate = admitted / "objects/info" / filename
                    if alternate.exists() or alternate.is_symlink():
                        fail("isolated recovery cannot borrow retiring objects")
                history = "exact shallow tip only; missing upstream ancestry not reconstructed"
            snapshot = repository_snapshot(admitted)
            if oid not in snapshot["reachable"]:
                fail(f"{source}: original raw ref object {oid} not reachable in {admitted}")
            if oid not in snapshot["types"]:
                snapshot["types"][oid] = git(admitted, "cat-file", "-t", oid).decode().strip()
            availability.append({"oid": oid, "object_type": snapshot["types"][oid],
                                 "admitted_repository": str(admitted), "history": history,
                                 "reachable_from_retained_refs": True,
                                 "retained_snapshot_sha256": digest(json.dumps(
                                     snapshot["state"], sort_keys=True).encode())})
        worktrees = git(source, "worktree", "list", "--porcelain").decode()
        for row in worktrees.splitlines():
            if row.startswith("worktree "):
                linked = Path(row[9:]).resolve()
                if linked != common and linked != parent and parent not in linked.parents:
                    fail(f"{source}: external worktree {linked}")
        nested.append({"path": display, "head": current,
                       "common_store": str(common), "canonical": str(target),
                       "refs": all_refs, "required_commit_availability": availability,
                       "worktrees": worktrees})
    return {"parent": str(parent), "head": head, "signature": signature,
            "retained_parent_refs": branch_refs,
            "status": os.fsdecode(raw_status).replace("\0", "\n"),
            "parent_refs": refs(parent), "nested": nested}, raw_status

def main():
    if len(sys.argv) != 2:
        fail("usage: collect.py ABSENT_ABSOLUTE_PACKET_DIRECTORY")
    destination = Path(sys.argv[1])
    if not destination.is_absolute() or destination.exists() or destination.is_symlink():
        fail("destination must be absolute and absent")
    if not destination.parent.is_dir() or destination.parent.resolve() != destination.parent:
        fail("destination parent must be existing and resolved")
    original = []
    metadata = []
    configs = []
    recipe_root = Path(__file__).resolve().parent
    recipe_inputs = [(recipe_root / name, (recipe_root / name).read_bytes())
                     for name in ("collect.py", "README.txt")]
    # Complete admission before creating any packet; no source/vendor/ref writes.
    for name in PARENTS:
        parent = BASE / name
        if parent.resolve() != parent or not parent.is_dir():
            fail(f"invalid exact parent {parent}")
        item, status = inspect(parent)
        item["diff_sha256"] = digest(git(parent, "diff", "--binary", "HEAD"))
        metadata.append(item)
        for filename in (".config", ".config.old"):
            source = parent / filename
            if source.exists() or source.is_symlink():
                if source.is_symlink() or not stat.S_ISREG(source.stat().st_mode):
                    fail(f"nonregular config {source}")
                data = source.read_bytes()
                configs.append((source, f"configs/{name}/{filename}", data))
    # Refuse incoming alternate edges to any private store selected for retirement.
    selected = {Path(n["common_store"]) / "objects"
                for item in metadata for n in item["nested"]}
    for admin in (CB / ".git", FW / ".git"):
        for alternate in admin.rglob("alternates"):
            if alternate.parent.name != "info" or alternate.parent.parent.name != "objects":
                continue
            for line in alternate.read_text().splitlines():
                target = Path(line)
                if not target.is_absolute():
                    target = alternate.parent.parent / target
                if target.resolve() in selected:
                    fail(f"incoming alternate {alternate} -> {target}")
    retained_before = {key: value["state"] for key, value in REPOSITORIES.items()}
    destination.mkdir(mode=0o755)
    for source, data in recipe_inputs:
        if source.read_bytes() != data:
            fail(f"recipe changed after admission {source}")
        relative = "recipes/" + source.name
        stored = destination / relative
        stored.parent.mkdir(parents=True, exist_ok=True)
        stored.write_bytes(data)
        original.append((digest(data), str(source), relative))
    for source, relative, data in configs:
        if source.read_bytes() != data:
            fail(f"config changed after admission {source}")
        stored = destination / relative
        stored.parent.mkdir(parents=True, exist_ok=True)
        stored.write_bytes(data)
        if stored.read_bytes() != data:
            fail(f"config copy differs {source}")
        original.append((digest(data), str(source), relative))
    (destination / "RECONSTRUCTION.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n")
    REPOSITORIES.clear()
    for item in metadata:
        parent = Path(item["parent"])
        current, unused_status = inspect(parent)
        if current != {key: value for key, value in item.items() if key != "diff_sha256"}:
            fail(f"metadata changed during collection {parent}")
        difference = git(parent, "diff", "--binary", "HEAD")
        if digest(difference) != item["diff_sha256"]:
            fail(f"parent diff changed {parent}")
        (destination / (parent.name + ".diff")).write_bytes(difference)
    retained_after = {key: value["state"] for key, value in REPOSITORIES.items()}
    if retained_before != retained_after:
        fail("retained canonical/recovery repository snapshot changed")
    (destination / "RETAINED_REPOSITORIES.json").write_text(
        json.dumps(retained_before, indent=2, sort_keys=True) + "\n")
    (destination / "ORIGINAL_SHA256SUMS").write_text(
        "".join(f"{sha}  {source}\n" for sha, source, rel in original))
    (destination / "FILES_MAP.tsv").write_text(
        "".join(f"{source}\t{rel}\tplain\n" for sha, source, rel in original))
    (destination / "QUALIFICATION.txt").write_text(
        "SOURCE/config recovery packet only; no historical gate success claim.\n"
        f"Exact retired parent count: {len(metadata)}; copied config count: {len(configs)}.\n"
        "Only .config/.config.old and two collector recipe files copied. Parent diffs/signatures/refs and nested\n"
        "HEAD/ref/store/canonical reachability are generated reconstruction metadata.\n"
        "No source, vendor trees, Git objects, build outputs, ROM/media, keys or binaries copied.\n"
        "Parent refs, canonical vendor refs and isolated shallow vboot recovery store must remain.\n"
        "vboot dd38 preserves incomplete history only; no missing parent is reconstructed.\n"
        "No Git GC/ref pruning authorized.\n")
    for source, data in recipe_inputs:
        if source.read_bytes() != data:
            fail(f"recipe changed during collection {source}")
    entries = []
    for stored in sorted(destination.rglob("*")):
        if stored.is_file():
            entries.append(f"{digest(stored.read_bytes())}  {stored.relative_to(destination)}\n")
    (destination / "ARCHIVE_SHA256SUMS").write_text("".join(entries))
    print(f"PASS parents={len(metadata)} configs={len(configs)} recipes={len(recipe_inputs)}")

if __name__ == "__main__":
    main()
