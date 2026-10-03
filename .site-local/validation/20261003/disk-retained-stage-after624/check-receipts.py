#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read-only archived receipts; no compiler, firmware, media or QEMU execution."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parent
OLD = "a9f4f6ea45cf13e4f54c8e8b7319b73a1ba3989f"
FINAL = "3b07610cc5a03d3d910217fdada7f624f64effc2"
OWNED = ("src/modules/dxe_core/entry.c", "tests/dxe_core_capsule_disk_handoff_test.sh",
         "tests/dxe_core_capsule_disk_stage_test.c", "tests/dxe_core_capsule_disk_stage_test.sh",
         "tests/dxe_core_capsule_disk_stage_test.ld")
LIFETIME = ("src/modules/fat/binding.c", "src/lib/capsule_disk.c")
INCLUDED = ("tests/dxe_core_capsule_disk_handoff_test.c",)


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def text(label, name):
    return (ROOT / "receipts" / label / name).read_text()


def archive(revision):
    with tarfile.open(ROOT / "source" / (revision + ".tar.gz"), "r:gz") as stream:
        members = stream.getmembers()
        require(all(m.isfile() or m.isdir() for m in members), "nonregular archive member")
        names = [m.name for m in members if m.isfile()]
        require(len(names) == len(set(names)), "duplicate regular archive member")
        files = {m.name: stream.extractfile(m).read() for m in members if m.isfile()}
    require(set(files) == set(OWNED + LIFETIME + INCLUDED), "unexpected source archive members")
    return files


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--originals", action="store_true")
    parser.add_argument("--git", type=Path)
    args = parser.parse_args()
    manifest = {}
    for line in (ROOT / "files.sha256").read_text().splitlines():
        expected, relative = line.split("  ", 1)
        if relative.startswith("./"):
            relative = relative[2:]
        path = ROOT / relative
        require(not Path(relative).is_absolute() and ".." not in Path(relative).parts,
                "unsafe manifest path")
        require(relative not in manifest and path.is_file() and not path.is_symlink(),
                "duplicate/missing/symlink manifest member")
        data = path.read_bytes()
        require(digest(data) == expected, "checksum mismatch: " + relative)
        require(not data.startswith((b"\x7fELF", b"MZ", b"!<arch>\n")), "binary payload")
        manifest[relative] = expected
    actual = {str(p.relative_to(ROOT)) for p in ROOT.rglob("*") if p.is_file()
              and p.name != "files.sha256"}
    require(actual == set(manifest), "unlisted/missing packet file")
    sources = {rev: archive(rev) for rev in (OLD, FINAL)}
    for path in OWNED[1:] + LIFETIME + INCLUDED:
        require(sources[OLD][path] == sources[FINAL][path], "source lineage drift: " + path)
    for label, revision in (("author-final", OLD), ("independent-final", OLD),
                            ("supported-619", OLD), ("joined-623", FINAL)):
        logname = "gate.log" if label == "independent-final" else (
            "handoff.log" if label in ("supported-619", "joined-623") else "stage.log")
        log = text(label, logname)
        compiled = {}
        for row in text(label, "compiler/inputs-before.sha256").splitlines():
            expected, original = row.split("  ", 1)
            compiled.setdefault(original, set()).add(expected)
        for name in (OWNED[0], OWNED[2], OWNED[3], OWNED[4]) + INCLUDED:
            matching = [values for original, values in compiled.items() if original.endswith("/" + name)]
            require(len(matching) == 1 and matching[0] == {digest(sources[revision][name])},
                    "compiled source/archive drift: " + label + "/" + name)
        copied_config = (ROOT / "receipts" / label / "compiler/config.h").read_bytes()
        config_matches = [values for original, values in compiled.items()
                          if original.endswith("/cdk2/config.h")]
        require(any(values == {digest(copied_config)} for values in config_matches),
                "compiler config drift")
        for cause, assertion in (("latch", "!filesystem_retired"),
                                 ("storage", "stop_calls == 1 && timer_calls == 0"),
                                 ("control", "pci_calls == 0")):
            require("Exact " + cause + " SAN guard discard: assertion134/full inverse/no sanitizer diagnostic PASS" in log,
                    "missing passing cause: " + label + "/" + cause)
            inverse = ROOT / "receipts" / label / "compiler" / (cause + ".inverse.c")
            require(inverse.read_bytes() == sources[revision][OWNED[0]], "inverse source drift")
            mutant = inverse.with_name(cause + ".c").read_bytes()
            require(mutant != inverse.read_bytes(), "unchanged mutant")
            cause_log = text(label, "compiler/" + cause + ".log")
            require(assertion in cause_log and "Assertion" in cause_log and "Aborted" in cause_log,
                    "wrong assertion receipt")
            require(not any(mark in cause_log for mark in (
                "AddressSanitizer", "LeakSanitizer", "UndefinedBehaviorSanitizer", "runtime error:")),
                    "sanitizer diagnostic is not an accepted cause")
        require(log.count("modeled controllers PASS") == 2, "missing O0/O2 positives")
        require(log.count("no completion of retired FS; one open/zero delete PASS") == 2,
                "missing scanner positives")
    for label in ("supported-619", "joined-623"):
        require(text(label, "handoff.status").strip() == "0", "supported gate failed")
        for guard in ("disk-delivery guard", "disk-support", "support-attributes",
                      "request-bit", "request-attributes", "request-consume"):
            require("DXE " + guard + " exact-source mutation: PASS" in text(label, "handoff.log"),
                    "missing handoff guard: " + guard)
    for label in ("p0-619", "p0-623"):
        require(text(label, "nonselected.status").strip() == "0", "P0 failed")
        require(text(label, "nonselected.log").count("UNSUPPORTED without callbacks PASS") == 2,
                "missing P0 O0/O2 scope")
    require(text("independent-final", "outer.status").strip() == "0", "independent status")
    for label, status in (("independent-awk-failure", "2"),
                          ("independent-link-failure", "1"),
                          ("independent-surviving-latch", "1")):
        require(text(label, "outer.status").strip() == status, "historical failure drift")
    require(text("supported-compile-failure", "handoff.status").strip() == "1", "compile failure drift")
    require("array subscript -1" in text("supported-compile-failure", "handoff.time"),
            "missing compiler failure")
    for name in ("normal-619.h", "p0.h"):
        cfg = (ROOT / "config" / name).read_text()
        on = "1" if name == "normal-619.h" else "0"
        for option in ("CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE", "CONFIG_CDK2_NATIVE_SYSTEM_FMP",
                       "CONFIG_PAYLOAD_DMA_HANDOFF"):
            require("#define " + option + " " + on + "\n" in cfg, "config scope drift")
        require("#define CONFIG_CDK2_LINEAR_BOOT 1\n" in cfg, "linear config drift")
    binding = sources[FINAL][LIFETIME[0]].decode()
    complete = sources[FINAL][LIFETIME[1]].decode().split("EFI_STATUS cdk2_capsule_disk_complete_esp(", 1)[1]
    require("binding->ops->release(binding->context, mount->simple_fs)" in binding,
            "missing real FAT lifetime source")
    require(complete.index("filesystem->open_volume == NULL") < complete.index("transaction->processing_status"),
            "completion preflight ordering drift")
    if args.originals:
        for line in (ROOT / "original-copies.tsv").read_text().splitlines():
            relative, original = line.split("\t")
            require((ROOT / relative).read_bytes() == Path(original).read_bytes(),
                    "original copy drift: " + original)
    if args.git:
        for revision, files in sources.items():
            for name, data in files.items():
                genuine = subprocess.check_output(["git", "-C", str(args.git), "show", revision + ":" + name])
                require(data == genuine, "Git/source archive mismatch")
    print("PASS: packet checksums, signed-source lineage, exact causes/inverses, preserved failures; HOST-only")


if __name__ == "__main__":
    main()
