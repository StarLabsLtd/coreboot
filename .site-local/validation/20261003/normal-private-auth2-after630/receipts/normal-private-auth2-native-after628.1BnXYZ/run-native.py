#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""One real normal-profile private AUTH2 boot, never HOST variable seeding."""

import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import uuid


STAGE = Path(__file__).resolve().parent
TEST = Path('/home/sean/Documents/.cdk2-worktrees/normal-private-auth2-older-append-after628')
TEST_HEAD = '4df1b4f47b3e36af7eb3ec2029d21c335d9dbbad'
FIRMWARE = Path('/home/sean/Documents/.cdk2-worktrees/default-zero-timeout-hotkey-after626')
FIRMWARE_HEAD = 'd8e49af2ab0c90c6f6542b29026bd80309a9e1da'
PRODUCER = Path('/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388')
PRODUCER_HEAD = '7ee34bed989c46913c3ee6672fb25e83227c3b6c'
OLD = Path('/home/sean/native-default-zero-hotkey-after628.CfqEvm')
CORE = OLD / 'native/cdk2-coreboot-image.elf'
ROM = OLD / 'initial9/build/coreboot.rom'
TOOL = OLD / 'initial9/build/util/cbfstool/cbfstool'
DISK = Path('/home/sean/Documents/cdk2-validation/qemu/pr331-627e3bbe/qemu/fixtures/nvme-final-c4bec.raw')
APP = STAGE / 'app/native/NormalPrivateAuth2OlderAppend.efi'
AUTH = STAGE / 'app/native/normal-private-auth2-inputs'
RUN = STAGE / 'run-1'

sys.path.insert(0, str(TEST / 'tests'))
sys.path.insert(0, str(TEST / 'util/qemu/bin'))
sys.path.insert(0, str(PRODUCER / 'util/efi_capsule'))
from system_fmp_core_ram_native_test import Qmp, digest, loads, save
from system_fmp_disk_retained_native_test import config_assignments
from qmp_cbmem_console import ConsoleReader, PhysicalReadError, TableNotReady
from qmp_cbmem_console import parse_table, validate_saved_console
from validate_capsule import parse_fmap_regions


class DeadlineQmp(Qmp):
    """Keep the existing observer protocol within this original VM budget."""

    def __init__(self, path, deadline):
        self.deadline = deadline
        super().__init__(path)

    def receive(self):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('original AUTH2 deadline during QMP receive')
        self.socket.settimeout(min(2.0, remaining))
        return super().receive()


MARKERS = [
    'CDK2_PUBLIC_FULLGRAPH_APP_ENTERED',
    'CDK2_NORMAL_PRIVATE_AUTH2_UNENROLLED_APP_ENTERED',
    'CDK2_PUBLIC_FULLGRAPH_READY_CLOSED_OP7_IF1_SET',
    'CDK2_PUBLIC_FULLGRAPH_REAL_EBS_CALLBACKS_LIVE',
    'CDK2_PUBLIC_FULLGRAPH_REAL_SET_VIRTUAL_MAP',
    'CDK2_PUBLIC_FULLGRAPH_RUNTIME_PHYSICAL_ALIASES_REMOVED',
    'CDK2_PUBLIC_FULLGRAPH_VIRTUAL_GET_SET_CLOSED_OP7',
    'CDK2_PUBLIC_FULLGRAPH_VIRTUAL_PRIVATE_AUTH2_CREATED_BOUND',
    'CDK2_PUBLIC_FULLGRAPH_VIRTUAL_PRIVATE_WRONG_AUTH2_DENIED_UNCHANGED',
    'CDK2_PUBLIC_FULLGRAPH_VIRTUAL_PRIVATE_AUTH2_APPENDED_BOUND',
    'CDK2_NORMAL_PRIVATE_AUTH2_OLDER_APPEND_REPLAY_DENIED_BOUND',
    'CDK2_NORMAL_PRIVATE_AUTH2_OLDER_APPEND_RUNTIME_PASS',
    'CDK2_PUBLIC_FULLGRAPH_RUNTIME_PASS',
]


def check_saved(directory):
    """Recheck actual saved bytes independently of QMP transport success."""
    body = validate_saved_console(directory)
    metadata = json.loads((directory / 'cbmem-observer.json').read_text())
    records = parse_table((directory / f'cbmem-table-{len(metadata["tables"]) - 1}.bin').read_bytes())
    firmware = [record[8:] for tag, record in records if tag == 0x45]
    expected = uuid.UUID('00112233-4455-6677-8899-aabbccddeeff').bytes_le
    expected += struct.pack('<III', 0x001a0009, 0x001a0009, 8388608)
    if firmware != [expected] or any(tag == 0x46 for tag, _ in records):
        raise ValueError('single real FW9 boot without retained capsule required')
    start = body.rfind(b'CDK2 | CAPSULE_RAM | begin |')
    if start < 0:
        raise ValueError('current real RAM phase boundary missing')
    current = body[start:]
    if re.search(rb'(?m)^CDK2 \| [A-Z][A-Z0-9_]* \| failed \|', current):
        raise ValueError('actual required firmware phase failed')
    offsets = []
    for phase, verb, status in (
            ('CAPSULE_RAM', 'complete', 'SUCCESS \\(0x0000000000000000\\)'),
            ('PCI_ROOTS', 'begin', 'NOT_STARTED \\(0x8000000000000013\\)'),
            ('CAPSULE_DISK', 'begin', 'NOT_STARTED \\(0x8000000000000013\\)'),
            ('CAPSULE_DISK', 'complete', 'SUCCESS \\(0x0000000000000000\\)'),
            ('BOOT_POLICY', 'complete', 'SUCCESS \\(0x0000000000000000\\)'),
            ('OS_HANDOFF', 'begin', 'NOT_STARTED \\(0x8000000000000013\\)')):
        pattern = rf'(?m)^CDK2 \| {phase} \| {verb} \| {status} \| [0-9]+ us\r?$'.encode()
        matches = list(re.finditer(pattern, current))
        if len(matches) != 1:
            raise ValueError('actual phase cardinality: ' + phase + '/' + verb)
        offsets.append(matches[0].start())
    if offsets != sorted(set(offsets)):
        raise ValueError('normal RAM/PCI/DISK/MAIN chronology rejected')
    for driver in ('VariableRuntimeDxe', 'EsrtDxe'):
        pattern = rf'(?m)^CDK2 \| {driver} \| required driver \| SUCCESS \(0x0000000000000000\) \| [0-9]+ us\r?$'.encode()
        if len(re.findall(pattern, body)) != 1:
            raise ValueError('genuine protected variable/ESRT installation missing')
    if b'SystemFmp CHECK transport' in current or b'SystemFmp SET transport' in current:
        raise ValueError('private AUTH2 gate must not invoke capsule CHECK/SET')
    uart = (directory / 'serial.log').read_bytes()
    actual = re.findall(rb'(?m)^(CDK2_(?:PUBLIC_FULLGRAPH|NORMAL_PRIVATE_AUTH2)_[A-Z0-9_]+)\r?$', uart)
    if actual != [marker.encode() for marker in MARKERS] or \
            uart.count(b'CDK2_PUBLIC_FULLGRAPH_') + uart.count(b'CDK2_NORMAL_PRIVATE_AUTH2_') != len(actual):
        raise ValueError('exact ordinary MAIN/runtime/private AUTH2 marker sequence rejected')
    result = json.loads((directory / 'result.json').read_text())
    if result.get('failure') is not None or result.get('qemu_status') != 3 or \
            result.get('budget_seconds') != 180 or type(result.get('wall_seconds')) not in (int, float) or \
            not 0 < result['wall_seconds'] <= 180 or \
            any(event.get('event') == 'RESET' for event in result.get('events', [])):
        raise ValueError('real guest success without RESET within original180 required')


if len(sys.argv) == 3 and sys.argv[1] == '--saved-only':
    check_saved(Path(sys.argv[2]))
    print('Saved strict FW9/phase/UART guest outcome check only; not another VM')
    raise SystemExit(0)
if len(sys.argv) != 1:
    raise SystemExit('usage: run-native.py [--saved-only RUN]')
if sys.flags.optimize or os.environ.get('PYTHONOPTIMIZE'):
    raise SystemExit('non-optimized native preflight required')
for source, head in ((TEST, TEST_HEAD), (FIRMWARE, FIRMWARE_HEAD), (PRODUCER, PRODUCER_HEAD)):
    if subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD']).decode().strip() != head or \
            subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain']).strip():
        raise ValueError('immutable clean test/actual firmware/producer source identity mismatch')
for directory in ('src', 'include', 'configs', 'Kconfig', 'defconfig'):
    changed = subprocess.check_output(['git', '-C', str(TEST), 'diff', '--name-only', FIRMWARE_HEAD,
                                      TEST_HEAD, '--', directory]).decode().splitlines()
    if changed and changed != ['src/boot/Makefile']:
        raise ValueError('selected production source/config changed: cannot reuse original Core')
if digest(CORE) != '6c86f953340891eac7f847508d9a65c6c67f0ac30c56905b5ee824a08282d08d' or \
        digest(ROM) != 'e381f1268fbc56e15a261fc7238206f97229430df4c4752cca192aac9fd697ae' or \
        digest(DISK) != '054a451e67b291adc7390301ef21c9df2de3582e768612cc66459e58d1305e36':
    raise ValueError('original actual normal628 Core/ROM/pristine NVMe identity mismatch')
for name in ('resolved.config', 'include/cdk2/config.h'):
    if (STAGE / 'app' / name).read_bytes() != (OLD / name).read_bytes():
        raise ValueError('app configuration differs from reused actual normal Core configuration')
header = (OLD / 'include/cdk2/config.h').read_text().splitlines()
for symbol, value in (('PROTECTED_VARIABLE_RUNTIME', 1), ('NATIVE_SYSTEM_FMP', 1),
                      ('NATIVE_SECURITY_STUB', 1), ('NATIVE_QEMU_TEST_FMP', 0),
                      ('QEMU_ACCEPTANCE_PROFILE', 0), ('BUILD_DEBUG', 0), ('BOOT_TIMEOUT', 0),
                      ('SECURE_BOOT', 0)):
    if f'#define CONFIG_CDK2_{symbol} {value}' not in header:
        raise ValueError('required honest normal profile setting: ' + symbol)
inventory = (OLD / 'native/native-direct-image-inventory.tsv').read_text()
if '975cd0e6-c540-4e2b-906c-72c0d0d1e40d' not in inventory or 'QEMU_TEST_FMP' in inventory:
    raise ValueError('actual normal SystemFmp image inventory required')
RUN.mkdir(exist_ok=False)
subprocess.run([str(TOOL), str(ROM), 'extract', '-n', 'fallback/payload', '-m', 'x86',
                '-f', str(RUN / 'extracted-original-core.elf')], check=True)
if loads(RUN / 'extracted-original-core.elf') != loads(CORE):
    raise ValueError('unchanged producer ROM does not contain original actual normal628 Core')
subprocess.run([str(TOOL), str(ROM), 'extract', '-n', 'config',
                '-f', str(RUN / 'packaged-producer.config')], check=True)
packaged = config_assignments((RUN / 'packaged-producer.config').read_text().splitlines())
if packaged != config_assignments((OLD / 'initial9/full.config').read_text().splitlines()):
    raise ValueError('actual packaged producer config differs from original bound full config')
for symbol in ('Q35_SMM_INVOCATION_NATIVE_PUBLIC_SERVICE_COMPONENT',
               'PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED', 'PAYLOAD_MM_AUTHVAR_STATE_PREDICATE_PINNED',
               'PAYLOAD_RESOURCE_HANDOFF', 'PAYLOAD_DMA_HANDOFF', 'DRIVERS_EFI_FW_INFO'):
    if packaged.get('CONFIG_' + symbol) != 'y':
        raise ValueError('genuine producer capability missing: ' + symbol)
tools = [Path(shutil.which(tool)) for tool in ('qemu-system-x86_64', 'mcopy', 'sh', 'sha256sum')]
inputs = [CORE, ROM, TOOL, DISK, APP, Path(__file__), STAGE / 'build-app.sh', Path(sys.executable),
          OLD / 'resolved.config', OLD / 'include/cdk2/config.h', OLD / 'initial9/full.config',
          OLD / 'native/native-direct-image-inventory.tsv', AUTH / 'private_certdb.bin',
          STAGE / 'app/native/cdk2-pe-exec-sections', STAGE / 'app/native/cdk2-native-pe-link',
          STAGE / 'app/source-before.sha256', STAGE / 'app/inputs-before.sha256',
          STAGE / 'app/tools-before.sha256', STAGE / 'app/outputs.sha256',
          STAGE / 'app/producer-before.sha256', STAGE / 'app/compile-closure-before.sha256',
          STAGE / 'app/vendor-heads.txt'] + tools
for source in (TEST, FIRMWARE, PRODUCER):
    paths = subprocess.check_output(['git', '-C', str(source), 'ls-files', '-z', 'src', 'include',
                                     'tests', 'util', 'configs', 'Makefile', 'Kconfig', 'defconfig'])
    inputs += [source / os.fsdecode(path) for path in paths.split(b'\0') if path and
               (source / os.fsdecode(path)).is_file()]
inputs += sorted(AUTH.glob('*_auth.bin')) + [AUTH / 'db.cert.pem', AUTH / 'private_appended_value.bin']
before = {str(path.resolve()): digest(path) for path in inputs}
(RUN / 'inputs-before.json').write_text(json.dumps(before, indent=2, sort_keys=True) + '\n')
flash, disk = RUN / 'pflash.rom', RUN / 'nvme.raw'
shutil.copyfile(ROM, flash)
shutil.copyfile(DISK, disk)
subprocess.run(['mcopy', '-o', '-i', str(disk) + '@@1048576', str(APP), '::/EFI/BOOT/BOOTX64.EFI'], check=True)
subprocess.run(['mcopy', '-i', str(disk) + '@@1048576', '::/EFI/BOOT/BOOTX64.EFI',
                str(RUN / 'staged-MAIN.efi')], check=True)
if (RUN / 'staged-MAIN.efi').read_bytes() != APP.read_bytes():
    raise ValueError('actual ordinary MAIN disk readback differs from the reviewed app')
disk_before = digest(disk)
initial = ROM.read_bytes()
regions = parse_fmap_regions(initial)
if len(initial) != 8388608 or regions.get('SMMSTORE') != (0, 65536):
    raise ValueError('actual bounded 8MiB ROM/64KiB SMMSTORE required')
(RUN / 'before-smmstore.bin').write_bytes(initial[:65536])
socket_dir = Path(tempfile.mkdtemp(prefix='normal-private-qmp.', dir='/tmp'))
command = [sys.executable, str(TEST / 'util/qemu/bin/run-deadline.py'), '--kill-after', '3', '180',
           '--', str(tools[0]), '-machine', 'q35,smm=on,accel=tcg', '-m', '1024M', '-cpu', 'max',
           '-smp', '1', '-nodefaults', '-device', 'intel-iommu,pt=off',
           '-device', 'VGA,bus=pcie.0,addr=01', '-drive', f'if=none,id=nvme0,format=raw,file={disk}',
           '-device', 'nvme,drive=nvme0,serial=CDK2ABNVME0001,bus=pcie.0,addr=03',
           '-device', 'qemu-xhci,bus=pcie.0,addr=04',
           '-device', 'edu,dma_mask=0xffffffff,bus=pcie.0,addr=05',
           '-global', 'driver=cfi.pflash01,property=secure,value=on',
           '-drive', f'if=pflash,format=raw,file={flash}', '-display', 'none', '-no-reboot',
           '-serial', f'file:{RUN / "serial.log"}', '-qmp', f'unix:{socket_dir / "qmp.sock"},server=on,wait=off',
           '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04']
(RUN / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
started = time.monotonic()
deadline = started + 180
failure = None
qmp = reader = None
with (RUN / 'qemu.log').open('wb') as log:
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    try:
        while process.poll() is None:
            if time.monotonic() >= deadline:
                raise TimeoutError('original oneboot AUTH2 180-second budget expired')
            uart = (RUN / 'serial.log').read_bytes() if (RUN / 'serial.log').exists() else b''
            if b'CDK2_PUBLIC_FULLGRAPH_RUNTIME_FAIL' in uart:
                raise ValueError('actual ordinary AUTH2 MAIN reported failure')
            if qmp is None and (socket_dir / 'qmp.sock').exists():
                qmp = DeadlineQmp(socket_dir / 'qmp.sock', deadline)
            if qmp is not None:
                qmp({'execute': 'query-memory-size-summary'})
                if qmp.epoch():
                    raise ValueError('unexpected guest reset before/during ordinary MAIN')
            # Snapshot only after the real app entered; all early phase text is
            # still present in CBMEM. Do not paper over active boot motion.
            if b'CDK2_PUBLIC_FULLGRAPH_APP_ENTERED\r\n' not in uart:
                time.sleep(0.01)
                continue
            if qmp is None:
                raise ValueError('actual MAIN entered without available read-only QMP')
            if reader is None:
                reader = ConsoleReader(qmp, RUN / 'scratch')
            try:
                body = reader.snapshot()
            except TableNotReady:
                time.sleep(0.01)
                continue
            except (PhysicalReadError, OSError, EOFError) as error:
                if process.poll() == 3 and reader.metadata is not None and \
                        (isinstance(error, (OSError, EOFError)) or isinstance(error.__cause__, OSError)):
                    break  # Strict saved bytes, phase and UART checks below are mandatory.
                raise
            if qmp.epoch():
                raise ValueError('unexpected guest reset in the oneboot private AUTH2 gate')
            save(reader, RUN, body)
            time.sleep(0.01)
        wall = time.monotonic() - started
        if process.poll() != 3 or not 0 < wall <= 180:
            raise ValueError('actual guest success3 within original180 required')
    except Exception as error:
        failure = repr(error)
        wall = time.monotonic() - started
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
        after = {path: digest(Path(path)) for path in before}
        (RUN / 'inputs-after.json').write_text(json.dumps(after, indent=2, sort_keys=True) + '\n')
        if before != after:
            failure = 'immutable source/config/Core/producer/app/tool inputs changed'
        result = {'failure': failure, 'qemu_status': process.returncode, 'budget_seconds': 180,
                  'wall_seconds': wall, 'events': qmp.events if qmp else [],
                  'original_firmware_head': FIRMWARE_HEAD, 'test_head': TEST_HEAD,
                  'original_core_sha256': digest(CORE), 'original_rom_sha256': digest(ROM),
                  'staged_disk_sha256': disk_before, 'after_disk_sha256': digest(disk),
                  'scope': 'normal SystemFmp/private AUTH2 oneboot; not enrollment/cold/cuts/EDK2 A-B/HW'}
        (RUN / 'result.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
if failure is not None:
    raise SystemExit(failure)
try:
    check_saved(RUN)
    installed = flash.read_bytes()
    if len(installed) != len(initial) or installed[65536:] != initial[65536:] or \
            installed[:65536] == initial[:65536] or digest(disk) != disk_before:
        raise ValueError('actual protected writes/outside-SMMSTORE/disk-baseline comparison rejected')
    (RUN / 'after-smmstore.bin').write_bytes(installed[:65536])
    with (RUN / 'private-media.log').open('wb') as log:
        subprocess.run(['sh', str(TEST / 'tests/protected_variable_private_media_check.sh'),
                        str(RUN / 'after-smmstore.bin'), str(AUTH / 'private_certdb.bin'),
                        str(RUN / 'private-media'), '--expect-live-older-append'], check=True,
                       env=dict(os.environ, COREBOOT_TREE=str(PRODUCER)),
                       stdout=log, stderr=subprocess.STDOUT)
    after = {path: digest(Path(path)) for path in before}
    if before != after:
        raise ValueError('immutable inputs changed during actual final-media HOST replay')
    (RUN / 'inputs-after.json').write_text(json.dumps(after, indent=2, sort_keys=True) + '\n')
except Exception as error:
    result['failure'] = repr(error)
    (RUN / 'result.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    raise
print('Actual normal-profile older private AUTH2 APPEND/denied replay + real final-media codec PASS')
