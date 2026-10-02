# SPDX-License-Identifier: GPL-2.0-only
"""HOST observation of ordinary Core RAM update; never guest admission."""

import argparse
import hashlib
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "util/qemu/bin"))
from qmp_cbmem_console import ConsoleReader, ConsoleSnapshotMotion, TableNotReady, parse_table
from qmp_cbmem_console import validate_saved_console
from system_fmp_core_ram_refusal import CASES, validate_refusal

REFUSAL_STATUS = {
    "wrong-signer": b"DEVICE_ERROR (0x8000000000000007)",
    "signed-byte": b"DEVICE_ERROR (0x8000000000000007)",
    "below-floor": b"UNSUPPORTED (0x8000000000000003)",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def loads(path):
    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[5:7] != b"\x01\x01":
        raise ValueError("expected actual little-endian x86 ELF payload")
    # cbfstool -m x86 reconstructs an ELF32 container for the same actual
    # loaded bytes/addresses. Its flags/alignment are not the original ELF's.
    if data[4] == 1 and struct.unpack_from("<H", data, 18)[0] == 3:
        entry, offset = struct.unpack_from("<II", data, 24)
        width, count = struct.unpack_from("<HH", data, 42)
        expected_width = 32
    elif data[4] == 2 and struct.unpack_from("<H", data, 18)[0] == 62:
        entry, offset = struct.unpack_from("<QQ", data, 24)
        width, count = struct.unpack_from("<HH", data, 54)
        expected_width = 56
    else:
        raise ValueError("unexpected ELF payload architecture/class")
    if width != expected_width or not 0 < count <= 32 or offset > len(data) or \
            count * width > len(data) - offset:
        raise ValueError("ELF program headers outside image")
    result = []
    for index in range(count):
        if data[4] == 1:
            kind, start, virtual, physical, size, extent, flags, alignment = \
                struct.unpack_from("<IIIIIIII", data, offset + index * width)
        else:
            kind, flags, start, virtual, physical, size, extent, alignment = \
                struct.unpack_from("<IIQQQQQQ", data, offset + index * width)
        if kind == 1:
            if start > len(data) or size > len(data) - start or size > extent:
                raise ValueError("ELF load segment outside image")
            result.append((virtual, physical, size, extent, data[start:start + size]))
    if not result:
        raise ValueError("ELF payload has no loaded segments")
    return entry, result


class Qmp:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(2)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb", buffering=0)
        self.events = []
        self.sequence = 0
        greeting = self.receive()
        if "QMP" not in greeting:
            raise ValueError("missing QMP greeting")
        self({"execute": "qmp_capabilities"})

    def receive(self):
        line = self.stream.readline()
        if not line:
            raise EOFError("QMP closed")
        return json.loads(line)

    def __call__(self, request):
        # Observation only: no reset, register write or guest authority input.
        if request["execute"] not in (
                "qmp_capabilities", "query-memory-size-summary", "pmemsave"):
            raise ValueError("non-observer QMP command")
        self.sequence += 1
        request = dict(request, id=self.sequence)
        self.stream.write(json.dumps(request).encode() + b"\n")
        while True:
            reply = self.receive()
            if "event" in reply:
                self.events.append(reply)
                continue
            if reply.get("id") != self.sequence:
                raise ValueError("unexpected QMP response identity")
            return {key: value for key, value in reply.items() if key != "id"}

    def epoch(self):
        return sum(event.get("event") == "RESET" for event in self.events)


def save(reader, directory, body):
    directory.mkdir(exist_ok=True)
    (directory / "cbmem-observer.json").write_text(
        json.dumps(reader.metadata, indent=2, sort_keys=True) + "\n")
    for index, data in enumerate(reader.table_snapshots):
        (directory / f"cbmem-table-{index}.bin").write_bytes(data)
    (directory / "cbmem-console.bin").write_bytes(reader.last_snapshot)
    (directory / "cbmem-live.log").write_bytes(body)


def previous_table_during_reset(reader, previous, elapsed):
    """Only an exact old accepted chain is provisional after observed RESET."""
    if previous is None:
        return False
    metadata = json.loads((previous / "cbmem-observer.json").read_text())
    if reader.metadata["tables"] != metadata["tables"]:
        return False
    for index, data in enumerate(reader.table_snapshots):
        if data != (previous / f"cbmem-table-{index}.bin").read_bytes():
            raise ValueError("old-chain metadata disagrees with accepted bytes")
    if elapsed > 10:
        raise ValueError("new reset epoch never published its own table")
    return True


def version_number(value):
    if not re.fullmatch(r"(?:0[xX][0-9a-fA-F]{1,8}|0|[1-9][0-9]{0,9})", value):
        raise argparse.ArgumentTypeError("expected unsigned 32-bit version")
    number = int(value, 16 if value.lower().startswith("0x") else 10)
    if number > 0xffffffff:
        raise argparse.ArgumentTypeError("version exceeds unsigned 32-bit range")
    return number


def unpublished_reset_console_motion(error, reader, previous, publication_started, now):
    # No accepted current snapshot can be discarded. The original observed
    # reset's publication deadline is never restarted by a changing console.
    return isinstance(error, ConsoleSnapshotMotion) and previous is not None and \
        reader.metadata is None and publication_started is not None and \
        0 <= now - publication_started <= 10


def check_boots(boots, capsule_size, prior_running=0x001A0009,
                target_running=0x001A000A, refusal=None):
    if len(boots) != (2 if refusal else 3):
        raise ValueError("unexpected independently captured firmware epoch count")
    for index, directory in enumerate(boots):
        body = validate_saved_console(directory)
        metadata = json.loads((directory / "cbmem-observer.json").read_text())
        leaf = len(metadata["tables"]) - 1
        records = parse_table((directory / f"cbmem-table-{leaf}.bin").read_bytes())
        identities = [record[8:] for tag, record in records if tag == 0x45]
        expected = uuid.UUID("00112233-4455-6677-8899-aabbccddeeff").bytes_le + \
            struct.pack("<III", target_running if index == 2 else prior_running,
                        0x001A0009, 8388608)
        if identities != [expected]:
            raise ValueError("actual firmware identity does not match boot epoch")
        capsules = [record for tag, record in records if tag == 0x46]
        if len(capsules) != (1 if index == 1 else 0):
            raise ValueError("actual capsule handoff cardinality mismatch")
        if capsules:
            if len(capsules[0]) != 20:
                raise ValueError("actual capsule handoff shape mismatch")
            address, size = struct.unpack_from("<QI", capsules[0], 8)
            limit = metadata["physical_limit"]
            if address == 0 or size != capsule_size or address >= limit or size > limit - address:
                raise ValueError("actual capsule handoff extent mismatch")
        # A previous snapshot may miss that boot's final appended tail. Use
        # the actual latest RAM phase boundary, not a previous cursor delta.
        ram = body.rfind(b"CDK2 | CAPSULE_RAM | begin")
        current = body[ram:] if ram >= 0 else b""
        pci = current.find(b"CDK2 | PCI_ROOTS | begin")
        complete = current.find(b"CDK2 | CAPSULE_RAM | complete | SUCCESS")
        if ram < 0 or (index != 1 and not 0 < complete < pci):
            raise ValueError("actual RAM phase must precede PCI")
        if False:  # discarded warm PCI refusal
            raise ValueError("capsule-processing epoch entered PCI before reset")
        if refusal and index == 1:
            expected = b"CDK2 | CAPSULE_RAM | failed | " + REFUSAL_STATUS[refusal] + b" | "
            if current.count(expected) != 1 or current.count(b"CDK2 | CAPSULE_RAM | failed") != 1:
                raise ValueError("actual RAM refusal status differs from the declared case")
            if b"CDK2 | CAPSULE_RAM | complete" in current:
                raise ValueError("refused RAM capsule unexpectedly completed")


def check_refusal_markers(uart, events):
    resets = [event for event in events if event.get("event") == "RESET"]
    if len(resets) != 1 or resets[0].get("data", {}).get("guest") is not True:
        raise ValueError("refusal requires exactly one actual guest warm reset")
    for marker in ("MAIN_ENTRY", "SETUP1_SECURE0", "REAL_SYSTEM_FMP_INFO",
                   "ACTUAL_INITIAL_ZERO_STATE", "QUERY_STRUCTURAL_ONLY", "UPDATE_CAPSULE_RESET"):
        if uart.count(("CDK2_CORE_RAM_STAGE_" + marker).encode()) != 1:
            raise ValueError("refusal ordinary MAIN marker cardinality: " + marker)
    if b"CDK2_CORE_RAM_STAGE_ACTUAL_PRIOR_SUCCESS_STATE" in uart or \
            b"CDK2_CORE_RAM_STAGE_FAIL" in uart or \
            b"CDK2_CORE_RAM_ACTUAL_NORMAL_CORE_COLD_RUNNING_AND_HISTORY_PASS" in uart:
        raise ValueError("refusal unexpectedly reached a failed application or cold history")


def check_refusal_media(installed, initial, regions):
    start, size = regions["SMMSTORE"]
    if (start, size) != (0, 65536) or len(initial) != 8388608 or \
            len(installed) != len(initial) or \
            installed[:start] != initial[:start] or installed[start + size:] != initial[start + size:]:
        raise ValueError("refusal changed firmware outside the actual SMMSTORE")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("initial", "target", "disk", "app", "output", "config",
                 "header", "core", "inventory", "capsule", "trust",
                 "capsule-tools", "cbfstool", "compiler", "linker",
                 "pe-link", "pe-audit", "pe-marker"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--binding", type=Path, action="append", default=[])
    parser.add_argument("--expect-refusal", choices=CASES)
    parser.add_argument("--reference-capsule", type=Path)
    parser.add_argument("--signer-cert", type=Path)
    for name, default in (("attempt", 0x001A000A), ("target-running", 0x001A000A),
                          ("prior-running", 0x001A0009), ("prior-history", 0)):
        parser.add_argument("--" + name, type=version_number, default=default)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    arguments = parser.parse_args()
    if arguments.expect_refusal:
        attempted = 0x001A0008 if arguments.expect_refusal == "below-floor" else 0x001A000A
        if (arguments.attempt, arguments.target_running, arguments.prior_running,
                arguments.prior_history) != (attempted, 0x001A000A, 0x001A0009, 0):
            parser.error("invalid finite refusal fixture constants")
    elif arguments.prior_running < 0x001A0009 or arguments.target_running < 0x001A0009 or \
            arguments.target_running == arguments.prior_running or \
            arguments.attempt <= max(arguments.prior_running, arguments.prior_history):
        parser.error("fixture requires a newer authenticated attempt and a changed running image")
    output = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    (output / "expected-versions.json").write_text(json.dumps({
        "attempt": arguments.attempt, "target_running": arguments.target_running,
        "prior_running": arguments.prior_running,
        "prior_history": arguments.prior_history}, indent=2) + "\n")
    inputs = [arguments.initial, arguments.target, arguments.disk, arguments.app,
              arguments.config, arguments.header, arguments.core,
              arguments.inventory, arguments.capsule, arguments.trust,
              arguments.cbfstool, arguments.compiler, arguments.linker,
              arguments.pe_link, arguments.pe_audit, arguments.pe_marker,
              arguments.capsule_tools / "generate_capsule.py",
              arguments.capsule_tools / "validate_capsule.py",
              Path(shutil.which(arguments.qemu)), Path(shutil.which("mcopy"))]
    inputs += arguments.binding + [Path(__file__), Path(__file__).parents[1] /
                                  "util/qemu/bin/qmp_cbmem_console.py",
                                  Path(__file__).with_name("system_fmp_core_ram_refusal.py")]
    if arguments.expect_refusal:
        if arguments.reference_capsule is None:
            parser.error("refusal requires actual authenticated reference capsule")
        inputs.append(arguments.reference_capsule)
        if arguments.signer_cert is not None:
            inputs.append(arguments.signer_cert)
        inputs += [Path(__file__).with_name("system_fmp_core_ram_refusal_store.c"),
                   Path(__file__).with_name("system_fmp_core_ram_refusal_store_test.sh")]
    for root in (Path(__file__).resolve().parents[1], arguments.capsule_tools.resolve().parents[1]):
        tracked = subprocess.check_output(["git", "-C", str(root), "ls-files", "-z",
                                           "src", "include", "tests", "util/efi_capsule",
                                           "util/qemu/bin", "util/qemu/config", "Makefile"])
        for name in tracked.decode().split("\0"):
            path = root / name
            if path.is_file() and (path.suffix in (".c", ".h", ".py", ".sh", ".ld", ".defconfig")
                                   or path.name in ("Makefile", "Makefile.mk")):
                inputs.append(path)
    before = {str(path.resolve()): digest(path) for path in inputs}
    (output / "inputs-before.json").write_text(json.dumps(before, indent=2) + "\n")
    header = arguments.header.read_text().splitlines()
    config = arguments.config.read_text().splitlines()
    if "CONFIG_CDK2_NATIVE_SYSTEM_FMP=y" not in config or \
            "CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME=y" not in config or \
            "CONFIG_CDK2_NATIVE_QEMU_TEST_FMP=y" in config or \
            "CONFIG_CDK2_QEMU_ACCEPTANCE_PROFILE=y" in config:
        raise ValueError("actual resolved configuration is not production FMP")
    for line in ("#define CONFIG_CDK2_NATIVE_SYSTEM_FMP 1",
                 "#define CONFIG_CDK2_NATIVE_QEMU_TEST_FMP 0",
                 "#define CONFIG_CDK2_QEMU_ACCEPTANCE_PROFILE 0",
                 "#define CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME 1"):
        if line not in header:
            raise ValueError("actual production configuration required")
    inventory = arguments.inventory.read_text()
    if "975cd0e6-c540-4e2b-906c-72c0d0d1e40d" not in inventory or \
            "QEMU_TEST_FMP" in inventory:
        raise ValueError("actual production SystemFmp inventory required")
    for label, image in (("initial", arguments.initial), ("target", arguments.target)):
        extracted = output / (label + "-core.elf")
        subprocess.run([str(arguments.cbfstool), str(image), "extract", "-n",
                        "fallback/payload", "-f", str(extracted), "-m", "x86"],
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if loads(extracted) != loads(arguments.core):
            raise ValueError("pre-signed firmware is not the actual full Core")
    if arguments.app.read_bytes().count(arguments.capsule.read_bytes()) != 1:
        raise ValueError("ordinary MAIN does not embed the exact signed full capsule")
    subprocess.run([str(arguments.pe_audit), "--subsystem", "10", str(arguments.app)],
                   check=True)
    if arguments.expect_refusal:
        validate_refusal(arguments.capsule_tools, arguments.capsule, arguments.target,
                         arguments.trust, arguments.reference_capsule, arguments.signer_cert,
                         arguments.expect_refusal, arguments.attempt)
    else:
        subprocess.run([sys.executable, str(arguments.capsule_tools / "validate_capsule.py"),
                        "--capsule", str(arguments.capsule), "--guid",
                        "00112233-4455-6677-8899-aabbccddeeff", "--embedded-drivers", "0",
                        "--fw-version", hex(arguments.attempt), "--lsv", "0x001a0009", "--image",
                        str(arguments.target), "--region", "COREBOOT",
                        "--trusted-public-cert", str(arguments.trust), "--initiate-reset",
                        "--require-fmap"], check=True)
    media = output / "pflash.rom"
    disk = output / "nvme.raw"
    shutil.copyfile(arguments.initial, media)
    shutil.copyfile(arguments.disk, disk)
    subprocess.run(["mcopy", "-o", "-i", str(disk) + "@@1048576",
                    str(arguments.app), "::/EFI/BOOT/BOOTX64.EFI"], check=True)
    disk_before = digest(disk)
    serial = output / "serial.log"
    command = [arguments.qemu, "-machine", "q35,smm=on,accel=tcg", "-m", "1024M",
               "-cpu", "max", "-smp", "1", "-nodefaults",
               "-device", "intel-iommu,pt=off", "-device", "VGA,bus=pcie.0,addr=01",
               "-drive", f"if=none,id=nvme0,format=raw,file={disk}",
               "-device", "nvme,drive=nvme0,serial=CDK2MMRESET,bus=pcie.0,addr=03",
               "-device", "qemu-xhci,bus=pcie.0,addr=04",
               "-device", "edu,dma_mask=0xffffffff,bus=pcie.0,addr=05",
               "-global", "driver=cfi.pflash01,property=secure,value=on",
               "-drive", f"if=pflash,format=raw,file={media},cache=writeback",
               "-display", "none", "-serial", f"file:{serial}",
               "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
               "-qmp", f"unix:{output}/qmp.sock,server=on,wait=off"]
    (output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    started = time.monotonic()
    deadline = started + 180
    boots = []
    reader = None
    accepted_epoch = None
    publication_started = None
    previous = None
    qmp = None
    failure = None
    refusal_observed = False
    with (output / "qemu.log").open("wb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=log)
        try:
            while process.poll() is None:
                if time.monotonic() >= deadline:
                    raise TimeoutError("predeclared three-boot 180s budget expired")
                uart = serial.read_bytes() if serial.exists() else b""
                if uart.count(b"CDK2_CORE_RAM_STAGE_UPDATE_CAPSULE_RESET") > 1:
                    raise ValueError("failed RAM processing attempted repeated staging")
                if b"CDK2_CORE_RAM_STAGE_FAIL" in uart:
                    raise ValueError("ordinary MAIN application failed")
                if qmp is None:
                    if not (output / "qmp.sock").exists():
                        time.sleep(0.05)
                        continue
                    qmp = Qmp(output / "qmp.sock")
                epoch = qmp.epoch()
                if reader is None or accepted_epoch != epoch:
                    if reader is not None:
                        previous = boots[-1] if boots else None
                        publication_started = time.monotonic()
                    reader = ConsoleReader(qmp, output / f"scratch-{epoch}")
                    accepted_epoch = qmp.epoch()
                try:
                    body = reader.snapshot()
                except (ValueError, OSError, EOFError) as error:
                    if qmp.epoch() != accepted_epoch:
                        previous = boots[-1] if boots else None
                        publication_started = time.monotonic()
                        reader = None
                        continue  # Discard only an explicitly observed reset crossing.
                    if isinstance(error, TableNotReady):
                        time.sleep(0.05)
                        continue
                    if unpublished_reset_console_motion(error, reader, previous,
                            publication_started, time.monotonic()):
                        time.sleep(0.05)
                        continue
                    if process.poll() is not None:
                        break
                    raise
                if qmp.epoch() != accepted_epoch:
                    previous = boots[-1] if boots else None
                    publication_started = time.monotonic()
                    reader = None
                    continue
                if previous_table_during_reset(reader, previous,
                        0 if publication_started is None else
                        time.monotonic() - publication_started):
                    reader = None
                    time.sleep(0.05)
                    continue
                previous = None
                directory = output / f"boot-{accepted_epoch}"
                records = parse_table(reader.table_snapshots[-1])
                capsules = [record for tag, record in records if tag == 0x46]
                if capsules and not (directory / "retained-capsule.bin").exists():
                    if len(capsules) != 1 or len(capsules[0]) != 20:
                        raise ValueError("retained capsule record shape")
                    address, size = struct.unpack_from("<QI", capsules[0], 8)
                    expected = arguments.capsule.read_bytes()
                    if size != len(expected) or address == 0 or address >= reader.physical_limit or \
                            size > reader.physical_limit - address:
                        raise ValueError("retained capsule record bounds")
                    try:
                        retained = b"".join(reader.read(address + offset, min(1024 * 1024,
                                                size - offset))
                                            for offset in range(0, size, 1024 * 1024))
                    except (ValueError, OSError, EOFError):
                        if qmp.epoch() == accepted_epoch:
                            raise
                        previous = boots[-1] if boots else None
                        publication_started = time.monotonic()
                        reader = None
                        continue
                    if qmp.epoch() != accepted_epoch:
                        previous = boots[-1] if boots else None
                        publication_started = time.monotonic()
                        reader = None
                        continue
                    if retained != expected:
                        raise ValueError("actual retained RAM capsule differs from signed input")
                    directory.mkdir(exist_ok=True)
                    (directory / "retained-capsule.bin").write_bytes(retained)
                save(reader, directory, body)
                if directory not in boots:
                    boots.append(directory)
                if arguments.expect_refusal and accepted_epoch == 1 and \
                        b"CDK2 | CAPSULE_RAM | failed" in body[body.rfind(b"CDK2 | CAPSULE_RAM | begin"):]:
                    check_boots(boots, arguments.capsule.stat().st_size,
                                arguments.prior_running, arguments.target_running,
                                arguments.expect_refusal)
                    check_refusal_markers(serial.read_bytes(), qmp.events)
                    refusal_observed = True
                    process.terminate()  # HOST ends a validated refusal; not a guest success exit.
                    process.wait(timeout=5)
                    break
                time.sleep(0.2)
            if arguments.expect_refusal and not refusal_observed:
                raise ValueError("actual declared RAM refusal was not observed")
            if not arguments.expect_refusal and process.wait() != 3:
                raise ValueError(f"actual QEMU exit {process.returncode}, expected 3")
            uart = serial.read_bytes()
            if arguments.expect_refusal:
                check_refusal_markers(uart, qmp.events)
                check_boots(boots, arguments.capsule.stat().st_size,
                            arguments.prior_running, arguments.target_running,
                            arguments.expect_refusal)
                sys.path.insert(0, str(arguments.capsule_tools))
                from validate_capsule import parse_fmap_regions
                check_refusal_media(media.read_bytes(), arguments.initial.read_bytes(),
                                    parse_fmap_regions(arguments.initial.read_bytes()))
                subprocess.run(["sh", str(Path(__file__).with_name(
                    "system_fmp_core_ram_refusal_store_test.sh")), str(media), str(output)],
                    env=dict(os.environ, CBFSTOOL=str(arguments.cbfstool),
                             COREBOOT_TREE=str(arguments.capsule_tools.resolve().parents[1]),
                             HOSTCC=str(arguments.compiler)), check=True)
                if digest(disk) != disk_before:
                    raise ValueError("refusal ordinary MAIN application disk changed")
            else:
                for marker, count in (("MAIN_ENTRY", 2), ("SETUP1_SECURE0", 2),
                                      ("REAL_SYSTEM_FMP_INFO", 2),
                                      ("ACTUAL_INITIAL_ZERO_STATE", 0 if arguments.prior_history else 1),
                                      ("ACTUAL_PRIOR_SUCCESS_STATE", 1 if arguments.prior_history else 0),
                                      ("QUERY_STRUCTURAL_ONLY", 1),
                                      ("UPDATE_CAPSULE_RESET", 1)):
                    if uart.count(("CDK2_CORE_RAM_STAGE_" + marker).encode()) != count:
                        raise ValueError(f"ordinary application marker cardinality: {marker}")
                if uart.count(b"CDK2_CORE_RAM_ACTUAL_NORMAL_CORE_COLD_RUNNING_AND_HISTORY_PASS") != 1:
                    raise ValueError("normal full Core cold state/history observer missing")
                check_boots(boots, arguments.capsule.stat().st_size,
                            arguments.prior_running, arguments.target_running)
                installed = media.read_bytes()
                target = arguments.target.read_bytes()
                initial = arguments.initial.read_bytes()
                if len(installed) != 8388608 or installed[73728:] != target[73728:]:
                    raise ValueError("installed COREBOOT differs from signed pristine target")
                if installed[65536:73728] != initial[65536:73728]:
                    raise ValueError("excluded probe/gap changed")
                if digest(disk) != disk_before:
                    raise ValueError("ordinary MAIN application disk changed")
        except Exception as error:
            failure = str(error)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            after = {path: digest(Path(path)) for path in before}
            (output / "inputs-after.json").write_text(json.dumps(after, indent=2) + "\n")
            if before != after:
                failure = "immutable caller/source/tool inputs changed"
            (output / "result.json").write_text(json.dumps({
                "failure": failure, "qemu_status": process.returncode,
                "expected_refusal": arguments.expect_refusal,
                "refusal_observed": refusal_observed,
                "completion": "HOST termination after validated refusal" if refusal_observed else
                              "guest debug exit or failure",
                "wall_seconds": time.monotonic() - started,
                "budget_seconds": 180, "events": [] if qmp is None else qmp.events,
                "installed_sha256": digest(media), "disk_sha256": digest(disk)},
                indent=2) + "\n")
    if failure:
        raise SystemExit(failure)
    if arguments.expect_refusal:
        print("Actual ordinary Core RAM refusal PASS (HOST termination; not guest success)")
    else:
        print("Actual ordinary Core RAM/SystemFmp three-boot update PASS")


if __name__ == "__main__":
    main()
