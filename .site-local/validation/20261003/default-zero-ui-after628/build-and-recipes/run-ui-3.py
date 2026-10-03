#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fresh normal default-zero observer, own media, no request or NVRAM seeds."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

if sys.flags.optimize or os.environ.get("PYTHONOPTIMIZE"):
    raise SystemExit("native UI preflight requires non-optimized Python")
stage = Path(__file__).resolve().parent
source = Path('/home/sean/Documents/.cdk2-worktrees/default-zero-timeout-hotkey-after626')
observer = Path('/home/sean/Documents/.cdk2-worktrees/default-zero-native-ui-observer-after628')
observer_head = 'f4d82376db41b79aedf1c81026f3db0ec601e41a'
assert subprocess.check_output(['git', '-C', str(observer), 'rev-parse', 'HEAD']).decode().strip() == observer_head
assert not subprocess.check_output(['git', '-C', str(observer), 'status', '--porcelain']).strip()
assert not subprocess.check_output([
    'git', '-C', str(observer), 'diff',
    'd8e49af2ab0c90c6f6542b29026bd80309a9e1da', observer_head,
    '--', 'src', 'include', 'configs', 'Makefile']).strip()
if len(sys.argv) != 2 or sys.argv[1] not in ('default-zero-hotkey', 'default-zero-no-key'):
    raise SystemExit('usage: run-ui.py default-zero-hotkey|default-zero-no-key')
mode = sys.argv[1]
run = stage / (mode + '-3')
assert not run.exists()
run.mkdir()
for name in ('selecting-reference.ppm', 'selecting-reference-manifest.json',
             'coreboot-config.txt', 'logo.bmp'):
    path = stage / 'reference' / name
    if path.exists():
        shutil.copyfile(path, run / name)
shutil.copyfile(stage / 'resolved.config', run / 'cdk2-config.txt')
manifest = json.loads((run / 'selecting-reference-manifest.json').read_bytes())
fields = {
    'config_header_sha256': stage / 'include/cdk2/config.h',
    'prepared_config_sha256': stage / 'resolved.config',
    'prepared_kconfig_sha256': stage / 'kconfig/Kconfig',
    'prepared_defconfig_sha256': stage / 'kconfig/defconfig',
    'coreboot_input_identity_sha256': stage / 'kconfig/coreboot-input.identity',
    'coreboot_source_input_sha256': stage / 'kconfig/coreboot-source.tmp',
    'lvgl_config_sha256': source / 'configs/lvgl/lv_conf.h',
    'native_ui_test_sha256': stage / 'native/cdk2-lvgl-ui-driver-test',
    'reference_test_source_sha256': source / 'tests/lvgl_ui_driver_test.c',
    'renderer_source_sha256': source / 'src/modules/lvgl_renderer/driver.c',
    'settings_source_sha256': source / 'src/modules/lvgl_setup/settings.c',
    'boot_logo_source_sha256': source / 'src/lib/boot_logo.c',
    'reference_sha256': run / 'selecting-reference.ppm',
    'producer_config_sha256': run / 'coreboot-config.txt',
}
for name, path in fields.items():
    assert manifest[name] == hashlib.sha256(path.read_bytes()).hexdigest()
assert manifest['source_commit'] == 'd8e49af2ab0c90c6f6542b29026bd80309a9e1da'
assert manifest['lvgl_commit'] == subprocess.check_output([
    'git', '-C', str(source), 'ls-tree', 'HEAD', '3rdparty/lvgl']).decode().split()[2]
assert manifest['ui_scale'] == 0
source_identity = json.loads((stage / 'reference-source-identities.json').read_bytes())
assert source_identity['both_clean_heads'] == manifest['source_commit']
for name, digest in source_identity['equal_reference_sources'].items():
    assert hashlib.sha256((source / name).read_bytes()).hexdigest() == digest
    assert hashlib.sha256((Path(source_identity['canonical']) / name).read_bytes()).hexdigest() == digest
assert hashlib.sha256((stage / 'native/cdk2-coreboot-image.elf').read_bytes()).hexdigest() == \
    '6c86f953340891eac7f847508d9a65c6c67f0ac30c56905b5ee824a08282d08d'
assert (stage / 'source-head.txt').read_text().strip() == manifest['source_commit']
rom = stage / 'initial9/build/coreboot.rom'
disk = Path('/home/sean/Documents/cdk2-validation/qemu/pr331-627e3bbe/qemu/fixtures/nvme-final-c4bec.raw')
usb = disk.with_name('usb.raw')
assert (run / 'selecting-reference-manifest.json').is_file() and not (run / 'result.json').exists()
assert subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD']).decode().strip() == 'd8e49af2ab0c90c6f6542b29026bd80309a9e1da'
assert not subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain']).strip()
header = (stage / 'include/cdk2/config.h').read_text()
assert '#define CONFIG_CDK2_BOOT_TIMEOUT 0\n' in header
observer_files = [observer / name for name in (
    'util/qemu/bin/qmp-setup-acceptance.py',
    'util/qemu/bin/setup_acceptance_contract.py',
    'util/qemu/bin/assert-default-zero-setup-run.py',
    'tests/default_zero_setup_controller_test.py',
    'util/qemu/bin/qmp_cbmem_console.py')]
# These HOST observer sources are separate from the clean compiled firmware WT.
(run / 'observer-source.json').write_text(json.dumps({
    'head': subprocess.check_output(['git', '-C', str(observer), 'rev-parse', 'HEAD']).decode().strip(),
    'status': subprocess.check_output(['git', '-C', str(observer), 'status', '--porcelain']).decode(),
    'sha256': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in observer_files},
}, indent=2, sort_keys=True) + '\n')
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
inputs += observer_files + [run / 'cdk2-config.txt']
inputs += list(fields.values()) + [stage / 'reference-source-identities.json']
inputs += [source / name for name in subprocess.check_output(
    ['git', '-C', str(source), 'ls-files', 'src', 'include', 'tests', 'util/qemu']).decode().splitlines()
           if (source / name).is_file()]
before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
(run / 'inputs-before.json').write_text(json.dumps(before, indent=2, sort_keys=True) + '\n')
for original, target in ((rom, run / 'pflash.rom'), (disk, run / 'nvme.raw'), (usb, run / 'usb.raw')):
    shutil.copyfile(original, target)
socket_dir = Path(tempfile.mkdtemp(prefix='normal-ui-qmp.', dir='/tmp'))
command = [qemu, '-no-shutdown', '-machine', 'q35,smm=on,accel=tcg', '-m', '1024M', '-cpu', 'max', '-smp', '1',
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
        observed = subprocess.run([sys.executable, str(observer / 'util/qemu/bin/qmp-setup-acceptance.py'),
                                   str(run), str(socket_dir / 'qmp.sock'), '180', mode, 'quiet'],
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
        if subprocess.check_output(['git', '-C', str(observer), 'rev-parse', 'HEAD']).decode().strip() != observer_head or \
                subprocess.check_output(['git', '-C', str(observer), 'status', '--porcelain']).strip():
            failure = 'frozen signed observer identity changed'
        (run / 'result.json').write_text(json.dumps({
            'failure': failure, 'observer_status': observer_status, 'qemu_status': process.returncode,
            'wall_seconds': time.monotonic() - start, 'deadline_seconds': 180,
            'scope': 'normal628 default-zero ' + mode + '; not delayed Selecting or BootToFwUI'}, indent=2) + '\n')
        shutil.rmtree(socket_dir)
if failure:
    raise SystemExit(failure)
subprocess.run([sys.executable, str(observer / 'util/qemu/bin/assert-default-zero-setup-run.py'),
                str(run), mode], check=True)
print('Actual normal628 default-zero ' + mode + ': bounded observer PASS')
