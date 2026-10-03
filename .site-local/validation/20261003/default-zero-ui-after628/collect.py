#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Collect a finite, reviewed allowlist; never copy firmware or private media."""
import hashlib
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
PACKET = ROOT / "default-zero-ui-after628"
STAGE = Path("/home/sean/native-default-zero-hotkey-after628.CfqEvm")
REPO = Path("/home/sean/Documents/.cdk2-worktrees/default-zero-valid-modes-after628")
OBSERVERS = (
    "956a7af7c8e2aca4e936cb549fc81bafa3999e74",
    "f4d82376db41b79aedf1c81026f3db0ec601e41a",
    "8a29709830889b9a7d1ab89c853653e9f39ba9e7",
    "c3d472f9c49138a5b2c8847644d2f74f31192dc5",
    "3f569b81a42c9f807e44ccd479ebf1f81b20f932",
    "c39ff97027953325ef6873e38ea19dd6ce2a36bd",
    "019ca1d4926c95613b074d92520a014e496c4647",
)
FAMILY = (
    "util/qemu/bin/qmp-setup-acceptance.py",
    "util/qemu/bin/setup_acceptance_contract.py",
    "util/qemu/bin/assert-default-zero-setup-run.py",
    "tests/default_zero_setup_controller_test.py",
    "util/qemu/bin/selftest.sh",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if PACKET.exists():
        raise ValueError("packet already exists; no overwrite allowed")
    PACKET.mkdir()
    originals = []

    def copy(source, destination):
        source = Path(source)
        if source.is_symlink() or not source.is_file():
            raise ValueError(f"not an original regular file: {source}")
        target = PACKET / destination
        if target.exists():
            raise ValueError(f"duplicate destination: {destination}")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        originals.append(f"{destination}\t{source}\t{digest(source)}\n")

    # Exact native output directory names are retained, including failures.
    runs = ("default-zero-hotkey", "default-zero-hotkey-2",
            "default-zero-no-key-2", "default-zero-no-key-3",
            "default-zero-queued-hotkey-4", "default-zero-queued-hotkey-5",
            "default-zero-queued-hotkey-6", "default-zero-queued-hotkey-7")
    names = {"result.json", "inputs-before.json", "inputs-after.json",
             "command.json", "observer-source.json", "observer.log",
             "serial.log", "qemu.log", "qmp-setup-evidence.json",
             "cdk2-config.txt", "coreboot-config.txt", "cbmem-live.log",
             "cbmem-console.bin", "cbmem-observer.json",
             "cbmem-table-0.bin", "cbmem-table-1.bin",
             "selecting-reference-manifest.json", "selecting-reference.ppm",
             "queued-input-console.log"}
    for run in runs:
        directory = STAGE / run
        for source in sorted(directory.iterdir()):
            if source.name in names or (source.name.startswith("setup-") and
                                        source.suffix in (".ppm", ".png")):
                copy(source, f"native/{run}/{source.name}")
        scratch = directory / "cbmem-scratch"
        for source in sorted(scratch.iterdir()):
            if source.name in {"publication-first.json", "publication-last.json",
                               "publication-first-table.bin", "publication-last-table.bin"}:
                copy(source, f"native/{run}/cbmem-scratch/{source.name}")

    # Top-level recipes/outer receipts are an explicit finite allowlist.
    top = "build-core.sh build-producer.sh build.log build.time resolved.config inputs-before.sha256 inputs-after-check.log outputs.sha256 source-before.sha256 source-after-check.log source-head.txt source-after.status vendor-heads.txt prepare-reference.py reference.log reference.time reference-build.log reference-config-before-after.json reference-source-identities.json run-ui.py run-ui-2.py run-ui-3.py run-ui-4.py run-ui-5.py run-ui-6.py run-ui-7.py held-native.log held-native.time held-native-2.log held-native-2.time no-key-native-2.log no-key-native-2.time no-key-native-3.log no-key-native-3.time queued-native-4.log queued-native-4.time default-zero-queued-hotkey-5-outer.log default-zero-queued-hotkey-5-outer.time default-zero-queued-hotkey-6-outer.log default-zero-queued-hotkey-6-outer.time default-zero-queued-hotkey-7-outer.log default-zero-queued-hotkey-7-outer.time default-zero-queued-hotkey-7-current-input-check.log convert-no-key-screens.py convert-run6-screens.py convert-run7-screens.py no-key-screens-conversion.json run6-screens-conversion.json run7-screens-conversion.json run-host.sh run-motion-host.sh run-queued-host.sh run-running-qmp-host.sh run-running-qmp-timing-host.sh run-actual-phase-order-host.sh run-retired-mode-host.sh".split()
    for name in top:
        copy(STAGE / name, f"build-and-recipes/{name}")
    for name in ("include/cdk2/config.h", "native/native-direct-image-inventory.tsv",
                 "native/native-direct-composition-inventory.tsv"):
        copy(STAGE / name, f"build-and-recipes/{name}")
    for name in "input.config full.config configure.log build.log build.time outputs.sha256 inputs-before.sha256 inputs-after-check.log source-before.sha256 source-after-check.log source-head.txt".split():
        copy(STAGE / "initial9" / name, f"producer-initial9/{name}")

    host_groups = {name: STAGE / name for name in (
        "host", "motion-host", "queued-host", "running-qmp-host",
        "running-qmp-timing-host", "actual-phase-order-host", "retired-mode-host")}
    host_groups.update({
        "production12": Path("/home/sean/default-zero-hotkey-proof.N3pnxd"),
        "peer-motion": Path("/home/sean/default-zero-motion-independent.VAy5gW"),
        "peer-queued": Path("/home/sean/default-zero-queued-independent.00xTk5"),
        "peer-running": Path("/home/sean/default-zero-running-qmp-independent.EnLlQe"),
        "peer-timing": Path("/home/sean/default-zero-qmp-timing-independent.h5RhoV"),
        "peer-order": Path("/home/sean/default-zero-phase-order-independent.LzKRfk"),
        "peer-retired": Path("/home/sean/default-zero-retired-mode-independent.gja5aD"),
    })
    allowed = {".log", ".time", ".sha256", ".txt", ".sh", ".py", ".patch"}
    for group, directory in host_groups.items():
        for source in sorted(directory.iterdir()):
            if source.is_file() and source.suffix in allowed:
                copy(source, f"host/{group}/{source.name}")
    probe = Path("/home/sean/qemu-paused-input-probe.POpTAl")
    for source in sorted(probe.iterdir()):
        if source.is_file() and source.suffix in allowed | {".json"}:
            copy(source, f"transport-probe/{source.name}")

    identities = []
    commits = []
    def archive(commit, paths):
        name = f"source/{commit}.tar"
        (PACKET / "source").mkdir(exist_ok=True)
        subprocess.run(["git", "-C", str(REPO), "archive", "--format=tar",
                        "-o", str(PACKET / name), commit, "--", *paths], check=True)
        commits.append(subprocess.check_output(["git", "-C", str(REPO), "log",
                       "-1", "--format=%H %G? %P", commit]).decode())
        for path in paths:
            data = subprocess.check_output(["git", "-C", str(REPO), "show", f"{commit}:{path}"])
            blob = subprocess.check_output(["git", "-C", str(REPO), "rev-parse", f"{commit}:{path}"]).decode().strip()
            identities.append(f"{name}\t{commit}\t{path}\t{blob}\t{hashlib.sha256(data).hexdigest()}\n")
    for commit in OBSERVERS:
        archive(commit, FAMILY + (("util/qemu/bin/qmp_cbmem_console.py",)
                                  if commit == OBSERVERS[-1] else ()))
    archive("d8e49af2ab0c90c6f6542b29026bd80309a9e1da", (
        "src/modules/bds/entry.c", "tests/bds_entry_test.c", "src/boot/Makefile",
        "src/lib/diagnostic.c", "src/modules/dxe_core/entry.c"))
    (PACKET / "source-identities.tsv").write_text("".join(identities))
    (PACKET / "source-commits.txt").write_text("".join(commits))
    (PACKET / "original-copies.tsv").write_text("".join(originals))
    for name in ("collect.py", "check-receipts.py", "README.md"):
        shutil.copyfile(ROOT / name, PACKET / name)
    rows = [f"{digest(path)}  {path.relative_to(PACKET)}\n"
            for path in sorted(PACKET.rglob("*")) if path.is_file()]
    (PACKET / "files.sha256").write_text("".join(rows))
    print(f"collected {len(rows)} finite files; {len(originals)} original copies")


if __name__ == "__main__":
    main()
