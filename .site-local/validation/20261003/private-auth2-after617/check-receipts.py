# SPDX-License-Identifier: GPL-2.0-only
"""Check finite copied receipts only; no build, VM, or omitted-media inference."""
import hashlib
import re
import struct
import subprocess
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def require(condition, description):
    if not condition:
        raise ValueError(description)


def text(relative):
    return (ROOT / relative).read_text()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def saved_manifest(relative):
    values = {}
    for line in text(relative).splitlines():
        expected, name = line.split("  ", 1)
        require(re.fullmatch(r"[0-9a-f]{64}", expected), relative)
        require(name not in values, "duplicate historical manifest path")
        values[name] = expected
    return values


records = saved_manifest("files.sha256")
for name, expected in records.items():
    path = ROOT / name
    require(not Path(name).is_absolute() and ".." not in Path(name).parts,
            "packet path escape")
    require(path.is_file() and digest(path.read_bytes()) == expected, name)
require({str(path.relative_to(ROOT)) for path in ROOT.rglob("*") if path.is_file()}
        == set(records) | {"files.sha256"}, "unmanifested packet file")

source_counts = {}
for archive in sorted((ROOT / "sources").glob("*.tar.zst")):
    entries = {}
    for line in archive.with_suffix("").with_suffix(".tsv").read_text().splitlines():
        revision, blob, name = line.split("\t")
        require(re.fullmatch(r"[0-9a-f]{40}", revision) and
                re.fullmatch(r"[0-9a-f]{40}", blob), "Git identity")
        require(name not in entries, "duplicate source identity")
        entries[name] = blob
    process = subprocess.Popen(["zstd", "-q", "-d", "-c", str(archive)],
                               stdout=subprocess.PIPE)
    seen = set()
    with tarfile.open(fileobj=process.stdout, mode="r|") as source:
        for member in source:
            if member.isdir():
                continue
            require(member.isfile() and member.name in entries and
                    member.name not in seen, "unexpected source member")
            data = source.extractfile(member).read()
            blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
            require(blob == entries[member.name], member.name)
            seen.add(member.name)
    require(process.wait() == 0 and seen == set(entries), "source archive closure")
    source_counts[archive.name] = len(seen)
require(sorted(source_counts.values()) == [6, 6, 6, 18], "finite source archive sizes")

require(text("fresh/outer.status").strip() == "0", "fresh outer status")
require(text("fresh/native.time").strip() ==
        "WALL=105.11 USER=90.30 SYS=21.44 PEAK_KIB=216888 EXIT=0", "fresh GNU receipt")
require(text("failure-prevm/outer.status").strip() == "2", "pre-VM failure status")
require("WRMSR byte count mismatch" in text("failure-prevm/build.log"), "pre-VM audit failure")
require(text("failure-oracle/outer.status").strip() == "134", "old oracle failure status")
require("matches(media)" in text("failure-oracle/codec-assertion.log"), "old assertion")
require(text("fresh/source-after.status") == "", "fresh clean working source receipt")
require(text("fresh/consumer-base.txt").strip() ==
        "9120b63d0430af6f0239856175871022f96bc37c", "actual consumer")
require(text("fresh/producer-head.txt").strip() ==
        "7ee34bed989c46913c3ee6672fb25e83227c3b6c", "actual producer")
require(text("fresh/run/source-before.sha256") == text("fresh/run/source-after.sha256"),
        "copied source before/after equality")
config = text("fresh/run/config.h")
for setting in ("QEMU_ACCEPTANCE_PROFILE 1", "PROTECTED_VARIABLE_RUNTIME 1",
                "NATIVE_SECURITY_STUB 1", "NATIVE_QEMU_TEST_FMP 1", "SECURE_BOOT 0",
                "NATIVE_SYSTEM_FMP 0", "STRICT_DIRECT_RUNTIME 1", "LINEAR_BOOT 1"):
    require("#define CONFIG_CDK2_" + setting in config, "qualified profile " + setting)

COMMON = ("APP_ENTERED READY_CLOSED_OP7_IF1_SET REAL_EBS_CALLBACKS_LIVE "
          "REAL_SET_VIRTUAL_MAP RUNTIME_PHYSICAL_ALIASES_REMOVED "
          "VIRTUAL_GET_SET_CLOSED_OP7 RUNTIME_PASS").split()
ENROLLED = ("AFTER_READY_SETUP_PK_ACCEPTED AFTER_READY_PK_TRUSTED_KEK_ACCEPTED "
            "AFTER_READY_KEK_TRUSTED_DB_ACCEPTED AFTER_READY_ENROLLED_SECURE_BOOT_ON "
            "AFTER_READY_UNSIGNED_CHILD_DENIED AFTER_READY_WRONG_SIGNER_CHILD_DENIED "
            "SIGNED_CHILD_EXECUTED AFTER_READY_SIGNED_CHILD_RETURNED "
            "VIRTUAL_NEWER_AUTH2_ACCEPTED VIRTUAL_AUTH2_REPLAY_DENIED_UNCHANGED "
            "VIRTUAL_WRONG_AUTH2_DENIED_UNCHANGED VIRTUAL_PRIVATE_AUTH2_CREATED_BOUND "
            "VIRTUAL_PRIVATE_WRONG_AUTH2_DENIED_UNCHANGED "
            "VIRTUAL_PRIVATE_AUTH2_REPLAY_DENIED_UNCHANGED "
            "VIRTUAL_PRIVATE_AUTH2_APPENDED_BOUND VIRTUAL_PRIVATE_AUTH2_DELETED_UNBOUND").split()
PHASES = ("DXE_SERVICES ARCH_PROTOCOLS VARIABLES_MIN CAPSULE_EARLY_GATE CAPSULE_RAM "
          "PLATFORM_TABLES PCI_ROOTS PCI_ENUMERATE STORAGE_CONTROLLERS BLOCK_DISCOVERY "
          "FILESYSTEMS CAPSULE_DISK DISPLAY_ADOPT INPUT_UI BOOT_POLICY OS_HANDOFF").split()
for case in ("ordinary", "enrolled"):
    lines = text("fresh/run/" + case + ".serial.log").splitlines()
    require(text("fresh/run/" + case + ".vm-status").strip() == "3", "guest status")
    require(not any(line.startswith("CDK2_PUBLIC_FULLGRAPH_RUNTIME_FAIL") for line in lines),
            "runtime failure marker")
    require(not any(re.match(r"CDK2 \| (" + "|".join(PHASES) + r") \| failed \| ", line)
                    for line in lines), "required phase failure")
    for marker in COMMON + (ENROLLED if case == "enrolled" else []):
        require(sum(line.startswith("CDK2_PUBLIC_FULLGRAPH_" + marker) for line in lines) == 1,
                case + ": " + marker)
    if case == "ordinary":
        require(not any(line.startswith("CDK2_PUBLIC_FULLGRAPH_AFTER_READY_") for line in lines),
                "ordinary unexpected enrollment")
        require(not any(re.match(r"CDK2_PUBLIC_FULLGRAPH_VIRTUAL_.*AUTH2", line) for line in lines),
                "ordinary unexpected AUTH2")
absent = text("fresh/run/absent.serial.log")
for marker in ("CDK2 | VariableRuntimeDxe | required driver | NOT_FOUND ",
               "CDK2 | VARIABLES_MIN | failed | NOT_FOUND ",
               "CDK2 | module 0x02 | variable policy protocols | 0x0000000000000000"):
    require(marker in absent, "absent endpoint predicate")
require("CDK2 | VariableRuntimeDxe | required driver | SUCCESS " not in absent and
        "CDK2_PUBLIC_FULLGRAPH_" not in absent, "absent unexpected app")
general = text("fresh/run/old-general.serial.log")
for marker in ("CDK2 | VariableRuntimeDxe | required driver | SUCCESS ",
               "CDK2 | EsrtDxe | required driver | UNSUPPORTED ",
               "CDK2 | VARIABLES_MIN | failed | UNSUPPORTED "):
    require(marker in general, "old GENERAL predicate")
require("CDK2 | EsrtDxe | required driver | SUCCESS " not in general and
        "CDK2_PUBLIC_FULLGRAPH_" not in general, "GENERAL unexpected app")
outputs = saved_manifest("fresh/run/native.outputs.sha256")
core = "8b0e097298c6e934151ac03afbb10b69e20170bce4f122956e10cdda9de126e9"
require(len(outputs) == 13 and len({value for name, value in outputs.items()
                                  if name.endswith(".elf")}) == 1,
        "same Core across four actual saved output bindings")
require(all(value == core for name, value in outputs.items() if name.endswith(".elf")), "Core SHA")
for name, value in outputs.items():
    if name.endswith(".serial.log"):
        require(digest((ROOT / "fresh/run" / Path(name).name).read_bytes()) == value,
                "actual serial output digest")
for optimization in ("0", "2"):
    require("PASS" in text("fresh/run/codec-check-o" + optimization + ".log"), "codec receipt")

der = (ROOT / "expected/db.cert.der").read_bytes()
def sequence(offset):
    require(offset + 2 <= len(der) and der[offset] == 0x30, "DER sequence")
    size, header = der[offset + 1], 2
    if size & 0x80:
        width = size & 0x7f
        require(0 < width <= 4 and offset + 2 + width <= len(der), "DER length")
        size = int.from_bytes(der[offset + 2:offset + 2 + width], "big")
        header += width
    end = offset + header + size
    require(end <= len(der), "DER bounds")
    return offset + header, end
tbs, end = sequence(0)
require(end == len(der), "complete certificate")
_, tbs_end = sequence(tbs)
binding = hashlib.sha256(b"native-service-db" + der[tbs:tbs_end]).digest()
name = "Cdk2PrivateAuth2Diagnostic".encode("utf-16-le")
node = bytes.fromhex("269f79375917494db8da1d6fc47b85c0") + \
    struct.pack("<III", 28 + len(name) + len(binding), len(name) // 2, len(binding)) + name + binding
require((ROOT / "expected/private.binding.bin").read_bytes() == binding, "independent binding")
require((ROOT / "expected/private_certdb.bin").read_bytes() ==
        struct.pack("<I", 4 + len(node)) + node, "independent certdb fixture")
print("Finite packet hashes, signed blobs, raw outcomes/profile and independent expected binding: PASS")
print("Saved/public receipts only; omitted private media is not re-decoded and no VM is run.")
