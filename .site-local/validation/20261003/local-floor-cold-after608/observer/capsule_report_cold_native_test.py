# SPDX-License-Identifier: GPL-2.0-only
"""Fresh normal cold boot of an actual successful warm report's owned copies."""

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

from system_fmp_core_ram_native_test import Qmp, check_boots, digest, loads, save

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "util/qemu/bin"))
from qmp_cbmem_console import ConsoleReader, PhysicalReadError, TableNotReady
from qmp_cbmem_console import parse_table, validate_saved_console


def check_cold_boot(directory, image_guid, uart, events, local_floor=False):
    if any(event.get("event") == "RESET" for event in events):
        raise ValueError("cold persistence must not reset")
    signature_marker = b"CDK2_CAPSULE_REPORT_COLD_PERSISTED_LOCKED_MAIN_PASS"
    local_marker = b"CDK2_CAPSULE_REPORT_COLD_LOCAL_FLOOR_PERSISTED_LOCKED_MAIN_PASS"
    if uart.count(b"CDK2_CAPSULE_REPORT_COLD_MAIN_ENTRY") != 1 or \
            uart.count(local_marker if local_floor else signature_marker) != 1 or \
            uart.count(signature_marker if local_floor else local_marker) != 0 or \
            b"CDK2_CAPSULE_REPORT_COLD_MAIN_FAIL" in uart:
        raise ValueError("cold ordinary MAIN observer marker cardinality")
    body = validate_saved_console(directory)
    metadata = json.loads((directory / "cbmem-observer.json").read_text())
    table = directory / f"cbmem-table-{len(metadata['tables']) - 1}.bin"
    records = parse_table(table.read_bytes())
    expected = image_guid + struct.pack("<III", 0x001A0009, 0x001A0009, 8388608)
    if [record[8:] for tag, record in records if tag == 0x45] != [expected]:
        raise ValueError("cold actual firmware identity must remain version9/floor9")
    if any(tag == 0x46 for tag, record in records):
        raise ValueError("cold boot still carries a capsule handoff")
    start = body.rfind(b"CDK2 | CAPSULE_RAM | begin")
    current = body[start:] if start >= 0 else b""
    if start < 0 or not 0 < current.find(b"CDK2 | CAPSULE_RAM | complete | SUCCESS") < \
            current.find(b"CDK2 | PCI_ROOTS | begin") or \
            re.search(rb"^CDK2 \| [A-Z][A-Z0-9_]* \| failed \|", current, re.MULTILINE):
        raise ValueError("cold normal RAM completion must precede PCI without a failed phase")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("warm", "capsule", "app", "output", "config", "header", "core",
                 "inventory", "cbfstool", "pe-audit", "capsule-tools", "compiler"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--binding", type=Path, action="append", default=[])
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--expect-local-floor-refusal", action="store_true")
    arguments = parser.parse_args()
    local_floor = arguments.expect_local_floor_refusal
    warm = arguments.warm.resolve()
    initial, original_disk = warm / "pflash.rom", warm / "nvme.raw"
    result = json.loads((warm / "result.json").read_text())
    if local_floor:
        expected_class = result.get("expected_local_floor_refusal") is True and \
            result.get("expected_certified_refusal") is None
    else:
        expected_class = result.get("expected_local_floor_refusal") is not True and \
            result.get("expected_certified_refusal") in ("wrong-signer", "signed-byte")
    if result.get("failure") is not None or result.get("qemu_status") != 3 or \
            result.get("expected_capsule_report") is not True or \
            result.get("refusal_observed") is not True or \
            result.get("expected_refusal") is not None or not expected_class or \
            result.get("budget_seconds") != 180 or not 0 <= result.get("wall_seconds", -1) <= 180:
        raise ValueError("actual successful warm report receipt required")
    if digest(initial) != result["installed_sha256"] or digest(original_disk) != result["disk_sha256"]:
        raise ValueError("warm media/disk differ from their actual final receipt")
    warm_before = json.loads((warm / "inputs-before.json").read_text())
    if warm_before != json.loads((warm / "inputs-after.json").read_text()) or \
            warm_before.get(str(arguments.capsule.resolve())) != digest(arguments.capsule):
        raise ValueError("prior capsule differs from actual immutable warm input")
    candidate = arguments.capsule.read_bytes()
    if (warm / "boot-1/retained-capsule.bin").read_bytes() != candidate:
        raise ValueError("prior warm retained request differs from expected report identity")
    check_boots(sorted(warm.glob("boot-*")), len(candidate),
                refusal="below-floor" if local_floor else result["expected_certified_refusal"],
                certified=not local_floor, local_floor=local_floor)
    warm_uart = (warm / "serial.log").read_bytes()
    if warm_uart.count(b"CDK2_CORE_RAM_STANDARD_CAPSULE_REPORT_MAIN_PASS") != 1:
        raise ValueError("prior ordinary MAIN did not observe a standard report")
    sys.path.insert(0, str(arguments.capsule_tools))
    from validate_capsule import parse_capsule_details, parse_fmap_regions
    image_guid = parse_capsule_details(candidate)["image_guid"].bytes_le
    regions = parse_fmap_regions(initial.read_bytes())
    if len(initial.read_bytes()) != 8388608 or regions.get("SMMSTORE") != (0, 65536):
        raise ValueError("actual closed 8MiB/SMMSTORE geometry required")
    for line in ("#define CONFIG_CDK2_NATIVE_SYSTEM_FMP 1",
                 "#define CONFIG_CDK2_NATIVE_QEMU_TEST_FMP 0",
                 "#define CONFIG_CDK2_QEMU_ACCEPTANCE_PROFILE 0",
                 "#define CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME 1"):
        if line not in arguments.header.read_text().splitlines():
            raise ValueError("actual production protected SystemFmp header required")
    config = arguments.config.read_text().splitlines()
    if "CONFIG_CDK2_NATIVE_SYSTEM_FMP=y" not in config or \
            "CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME=y" not in config or \
            "CONFIG_CDK2_NATIVE_QEMU_TEST_FMP=y" in config or \
            "CONFIG_CDK2_QEMU_ACCEPTANCE_PROFILE=y" in config or \
            "975cd0e6-c540-4e2b-906c-72c0d0d1e40d" not in arguments.inventory.read_text():
        raise ValueError("actual production resolved profile/inventory required")
    output = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    inputs = [initial, original_disk, warm / "result.json", warm / "inputs-before.json",
              warm / "inputs-after.json", arguments.capsule, arguments.app, arguments.config,
              arguments.header, arguments.core, arguments.inventory, arguments.cbfstool,
              arguments.pe_audit, arguments.compiler,
              Path(shutil.which(arguments.qemu)), Path(shutil.which("mcopy")),
              arguments.capsule_tools / "validate_capsule.py", Path(__file__),
              Path(__file__).with_name("system_fmp_core_ram_native_test.py"),
              Path(__file__).with_name("capsule_report_cold_store.c"),
              Path(__file__).with_name("capsule_report_cold_store_test.sh"),
              Path(__file__).with_name("system_fmp_core_ram_refusal_store.c"),
              Path(__file__).parents[1] / "util/qemu/bin/qmp_cbmem_console.py"] + arguments.binding
    for source_root in (Path(__file__).resolve().parents[1],
                        arguments.capsule_tools.resolve().parents[1]):
        tracked = subprocess.check_output(["git", "-C", str(source_root), "ls-files", "-z",
                                           "src", "include", "tests", "Makefile"])
        for name in tracked.decode().split("\0"):
            path = source_root / name
            if path.is_file() and (path.suffix in (".c", ".h", ".sh", ".py", ".ld") or
                                   path.name in ("Makefile", "Makefile.mk")):
                inputs.append(path)
    before = {str(path.resolve()): digest(path) for path in inputs}
    (output / "inputs-before.json").write_text(json.dumps(before, indent=2) + "\n")
    extracted = output / "actual-core.elf"
    subprocess.run([str(arguments.cbfstool), str(initial), "extract", "-n", "fallback/payload",
                    "-f", str(extracted), "-m", "x86"], check=True, capture_output=True)
    if loads(extracted) != loads(arguments.core):
        raise ValueError("successful warm firmware does not contain the actual normal Core")
    if arguments.app.read_bytes().count(candidate[:16]) != 1:
        raise ValueError("cold MAIN must bind the actual prior capsule GUID")
    subprocess.run([str(arguments.pe_audit), "--subsystem", "10", str(arguments.app)], check=True)
    codec_environment = dict(os.environ,
                             COREBOOT_TREE=str(arguments.capsule_tools.resolve().parents[1]),
                             CBFSTOOL=str(arguments.cbfstool), HOSTCC=str(arguments.compiler),
                             MM_COLD_CAPSULE_REPORT_LOCAL_FLOOR="1" if local_floor else "")
    codec = Path(__file__).with_name("capsule_report_cold_store_test.sh")
    report_before = subprocess.check_output(["sh", str(codec), str(initial), str(output)],
                                            env=codec_environment)
    if len(report_before) != 94:
        raise ValueError("actual producer report/Last records must contain exactly 72+22 bytes")
    (output / "report-last-before.bin").write_bytes(report_before)
    media, disk = output / "pflash.rom", output / "nvme.raw"
    shutil.copyfile(initial, media)
    shutil.copyfile(original_disk, disk)
    subprocess.run(["mcopy", "-o", "-i", str(disk) + "@@1048576",
                    str(arguments.app), "::/EFI/BOOT/BOOTX64.EFI"], check=True)
    disk_baseline = digest(disk)
    serial = output / "serial.log"
    command = [arguments.qemu, "-machine", "q35,smm=on,accel=tcg", "-m", "1024M",
               "-cpu", "max", "-smp", "1", "-nodefaults", "-device", "intel-iommu,pt=off",
               "-device", "VGA,bus=pcie.0,addr=01",
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
    qmp, reader, failure = None, None, None
    directory = output / "boot-0"
    with (output / "qemu.log").open("wb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=log)
        try:
            while process.poll() is None and time.monotonic() < deadline:
                if qmp is None:
                    if not (output / "qmp.sock").exists():
                        time.sleep(0.05)
                        continue
                    qmp = Qmp(output / "qmp.sock")
                    reader = ConsoleReader(qmp, output / "scratch")
                try:
                    body = reader.snapshot()
                except TableNotReady:
                    time.sleep(0.05)
                    continue
                except (EOFError, OSError):
                    if process.poll() == 3:
                        break
                    raise
                except PhysicalReadError as error:
                    # QMP may close while a final read is in flight at guest exit.
                    # Semantic/truncated reads and every live or initial failure
                    # remain fatal; the saved cold oracle is still mandatory.
                    if process.poll() == 3 and reader.metadata is not None and \
                            isinstance(error.__cause__, OSError):
                        break
                    raise
                if qmp.epoch() != 0:
                    raise ValueError("unexpected reset during cold persistence")
                save(reader, directory, body)
                if re.search(rb"^CDK2 \| [A-Z][A-Z0-9_]* \| failed \|", body, re.MULTILINE):
                    raise ValueError("normal cold boot reached a failed phase")
                time.sleep(0.2)
            if process.poll() != 3 or time.monotonic() > deadline or qmp is None:
                raise ValueError("cold persistence requires actual guest exit3 within 180 seconds")
            check_cold_boot(directory, image_guid, serial.read_bytes(), qmp.events, local_floor)
            # Normal MTC reservation may legitimately update only SMMSTORE.
            if len(media.read_bytes()) != 8388608 or \
                    media.read_bytes()[65536:] != initial.read_bytes()[65536:]:
                raise ValueError("cold normal boot changed firmware outside SMMSTORE")
            if digest(disk) != disk_baseline:
                raise ValueError("cold ordinary MAIN changed its staged disk")
            report_after = subprocess.check_output(["sh", str(codec), str(media), str(output)],
                                                   env=codec_environment)
            (output / "report-last-after.bin").write_bytes(report_after)
            if report_after != report_before:
                raise ValueError("full report72/Last22 bytes changed across genuine cold boot")
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
                failure = "immutable warm donor/source/tool inputs changed"
            (output / "result.json").write_text(json.dumps({
                "failure": failure, "qemu_status": process.returncode,
                "wall_seconds": time.monotonic() - started, "budget_seconds": 180,
                "events": [] if qmp is None else qmp.events,
                "installed_sha256": digest(media), "disk_sha256": digest(disk),
                "disk_baseline_sha256": disk_baseline,
                "expected_local_floor_refusal": local_floor,
                "completion": "ordinary cold MAIN persisted report and normal locks" if failure is None
                              else "failed cold observation"}, indent=2) + "\n")
    if failure:
        raise SystemExit(failure)


if __name__ == "__main__":
    main()
