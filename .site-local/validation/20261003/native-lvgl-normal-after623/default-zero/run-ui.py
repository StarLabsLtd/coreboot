#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Normal-profile launch with unchanged existing QMP UI controller; no seeds."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

stage = Path(__file__).resolve().parent
source = Path('/home/sean/Documents/.cdk2-worktrees/phase-owned-lvgl-status-after614')
run = stage / 'ui-run'
rom = stage / 'initial9/build/coreboot.rom'
disk = Path('/home/sean/Documents/cdk2-validation/qemu/pr331-627e3bbe/qemu/fixtures/nvme-final-c4bec.raw')
usb = disk.with_name('usb.raw')
assert (run / 'selecting-reference-manifest.json').is_file() and not (run / 'result.json').exists()
assert subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD']).decode().strip() == 'cb71f948001ddfcc2b366d645b12a61e08d3ff1a'
assert not subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain']).strip()
header = (stage / 'include/cdk2/config.h').read_text()
for name in ('PROTECTED_VARIABLE_RUNTIME', 'NATIVE_SYSTEM_FMP', 'LVGL_RENDERER', 'LINEAR_SETUP_HOTKEY'):
    assert f'#define CONFIG_CDK2_{name} 1\n' in header
for name in ('NATIVE_QEMU_TEST_FMP', 'QEMU_ACCEPTANCE_PROFILE', 'BUILD_DEBUG'):
    assert f'#define CONFIG_CDK2_{name} 0\n' in header
# Verify actual packed fallback ELF loads against the freshly built Core,
# using the existing strict ELF32/64 reader (not raw file-shape equality).
sys.path.insert(0, str(source / 'tests'))
from system_fmp_core_ram_native_test import loads
tool = stage / 'initial9/build/util/cbfstool/cbfstool'
subprocess.run([str(tool), str(rom), 'extract', '-n', 'fallback/payload', '-m', 'x86',
                '-f', str(run / 'extracted-core.elf')], check=True)
assert loads(run / 'extracted-core.elf') == loads(stage / 'native/cdk2-coreboot-image.elf')
qemu = shutil.which('qemu-system-x86_64')
assert qemu
inputs = [rom, disk, usb, tool, Path(qemu), Path(sys.executable), stage / 'native/cdk2-coreboot-image.elf',
          stage / 'resolved.config', stage / 'include/cdk2/config.h', Path(__file__),
          stage / 'prepare-reference.py', run / 'selecting-reference.ppm',
          run / 'selecting-reference-manifest.json', run / 'coreboot-config.txt']
inputs += [source / name for name in subprocess.check_output(
    ['git', '-C', str(source), 'ls-files', 'src', 'include', 'tests', 'util/qemu']).decode().splitlines()
           if (source / name).is_file()]
before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
(run / 'inputs-before.json').write_text(json.dumps(before, indent=2, sort_keys=True) + '\n')
for original, target in ((rom, run / 'pflash.rom'), (disk, run / 'nvme.raw'), (usb, run / 'usb.raw')):
    shutil.copyfile(original, target)
socket_dir = Path(tempfile.mkdtemp(prefix='normal-ui-qmp.', dir='/tmp'))
command = [qemu, '-machine', 'q35,smm=on,accel=tcg', '-m', '1024M', '-cpu', 'max', '-smp', '1',
           '-nodefaults', '-device', 'intel-iommu,pt=off', '-device', 'VGA,id=cdk2-vga,bus=pcie.0,addr=01',
           '-drive', f'if=none,id=nvme0,format=raw,file={run / "nvme.raw"}',
           '-device', 'nvme,drive=nvme0,serial=CDK2ABNVME0001,bus=pcie.0,addr=03',
           '-device', 'qemu-xhci,id=xhci,bus=pcie.0,addr=04',
           '-drive', f'if=none,id=usb0,format=raw,file={run / "usb.raw"},cache=unsafe',
           '-device', 'usb-storage,bus=xhci.0,drive=usb0,serial=CDK2ABUSB0001,removable=off',
           '-device', 'usb-kbd,id=cdk2-usb-kbd,bus=xhci.0,display=cdk2-vga',
           '-device', 'usb-mouse,id=cdk2-usb-mouse,bus=xhci.0',
           '-device', 'edu,dma_mask=0xffffffff,bus=pcie.0,addr=05',
           '-global', 'driver=cfi.pflash01,property=secure,value=on',
           '-drive', f'if=pflash,format=raw,file={run / "pflash.rom"},cache=writeback',
           '-display', f'vnc=unix:{socket_dir / "vnc.sock"},id=cdk2-setup', '-S',
           '-serial', f'file:{run / "serial.log"}', '-qmp', f'unix:{socket_dir / "qmp.sock"},server=on,wait=off']
(run / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
environment = os.environ.copy()
environment['CDK2_AB_QMP_SELECTING_REFERENCE'] = str(run / 'selecting-reference.ppm')
environment['CDK2_AB_QMP_SELECTING_REFERENCE_MANIFEST'] = str(run / 'selecting-reference-manifest.json')
start = time.monotonic()
failure = None
observer_status = None
with (run / 'qemu.log').open('wb') as qemu_log, (run / 'observer.log').open('wb') as observer_log:
    process = subprocess.Popen(command, stdout=qemu_log, stderr=subprocess.STDOUT)
    try:
        observed = subprocess.run([sys.executable, str(source / 'util/qemu/bin/qmp-setup-acceptance.py'),
                                   str(run), str(socket_dir / 'qmp.sock'), '180', 'hotkey', 'quiet'],
                                  env=environment, stdout=observer_log, stderr=subprocess.STDOUT,
                                  timeout=max(0.001, 180 - (time.monotonic() - start)))
        observer_status = observed.returncode
        if observer_status:
            failure = f'existing QMP controller exit {observer_status}; preserve partial evidence'
        else:
            process.wait(timeout=5)
    except (subprocess.TimeoutExpired, OSError) as error:
        failure = repr(error)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        after = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
        (run / 'inputs-after.json').write_text(json.dumps(after, indent=2, sort_keys=True) + '\n')
        if before != after:
            failure = 'immutable caller/source/input change'
        (run / 'result.json').write_text(json.dumps({
            'failure': failure, 'observer_status': observer_status, 'qemu_status': process.returncode,
            'wall_seconds': time.monotonic() - start, 'deadline_seconds': 180,
            'scope': 'normal623 UI controller; not unchanged QEMU-acceptance whole suite'}, indent=2) + '\n')
        shutil.rmtree(socket_dir)
if failure:
    raise SystemExit(failure)
print('Existing QMP controller completed on actual normal623 profile; bounded visual oracle remains separate')
