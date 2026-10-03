#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Produce an actual normal-profile raster; do not create firmware authority."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

stage = Path(__file__).resolve().parent
source = Path('/home/sean/Documents/.cdk2-worktrees/default-zero-timeout-hotkey-after626')
canonical = Path('/home/sean/Documents/cdk2')
lvgl = Path('/home/sean/Documents/cdk2/3rdparty/lvgl')
producer = stage / 'initial9/build'
run = stage / 'reference'
assert not run.exists()
assert subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD']).decode().strip() == 'd8e49af2ab0c90c6f6542b29026bd80309a9e1da'
assert not subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain']).strip()
assert subprocess.check_output(['git', '-C', str(canonical), 'rev-parse', 'HEAD']).decode().strip() == 'd8e49af2ab0c90c6f6542b29026bd80309a9e1da'
assert not subprocess.check_output(['git', '-C', str(canonical), 'status', '--porcelain']).strip()
reference_sources = ['tests/lvgl_ui_driver_test.c', 'src/modules/lvgl_renderer/driver.c',
                     'src/modules/lvgl_setup/settings.c', 'src/lib/boot_logo.c', 'configs/lvgl/lv_conf.h',
                     'src/boot/Makefile', 'Makefile', 'tests/lvgl_dependency_provenance_test.sh']
identities = {}
for name in reference_sources:
    assert (source / name).read_bytes() == (canonical / name).read_bytes()
    identities[name] = hashlib.sha256((source / name).read_bytes()).hexdigest()
(stage / 'reference-source-identities.json').write_text(json.dumps({
    'worktree': str(source), 'canonical': str(canonical),
    'both_clean_heads': 'd8e49af2ab0c90c6f6542b29026bd80309a9e1da',
    'equal_reference_sources': identities}, indent=2, sort_keys=True) + '\n')
expected = subprocess.check_output(['git', '-C', str(source), 'ls-tree', 'HEAD', '3rdparty/lvgl']).decode().split()[2]
actual = subprocess.check_output(['git', '-C', str(lvgl), 'rev-parse', 'HEAD']).decode().strip()
assert expected == actual and not subprocess.check_output(['git', '-C', str(lvgl), 'status', '--porcelain']).strip()
config_files = [stage / 'resolved.config', stage / 'include/cdk2/config.h',
                stage / 'kconfig/coreboot-input.identity', stage / 'kconfig/coreboot-source.tmp',
                stage / 'kconfig/Kconfig', stage / 'kconfig/defconfig']
before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in config_files}
with (stage / 'reference-build.log').open('wb') as output:
    subprocess.run(['make', '-j2', '-C', str(canonical), 'native-lvgl-renderer-test',
                    f'CDK2_BUILD_DIR={stage}', f'CDK2_CONFIG={stage / "resolved.config"}',
                    'COREBOOT_TREE=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388',
                    'COREBOOT_CONFIG=/home/sean/disk-no-request-hex-producer-after619.pXSGcd/initial9/full.config',
                    'CDK2_BEARSSL_DIR=/home/sean/Documents/cdk2/3rdparty/bearssl', f'CDK2_LVGL_ROOT={lvgl}'],
                   stdout=output, stderr=subprocess.STDOUT, check=True)
assert before == {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in config_files}
assert subprocess.check_output(['git', '-C', str(canonical), 'rev-parse', 'HEAD']).decode().strip() == 'd8e49af2ab0c90c6f6542b29026bd80309a9e1da'
assert not subprocess.check_output(['git', '-C', str(canonical), 'status', '--porcelain']).strip()
for name, digest in identities.items():
    assert hashlib.sha256((canonical / name).read_bytes()).hexdigest() == digest
    assert hashlib.sha256((source / name).read_bytes()).hexdigest() == digest
run.mkdir()
subprocess.run([str(producer / 'util/cbfstool/cbfstool'), str(producer / 'coreboot.rom'),
                'extract', '-n', 'config', '-f', str(run / 'coreboot-config.txt')], check=True)
config = (run / 'coreboot-config.txt').read_bytes()
environment = os.environ.copy()
environment.pop('CDK2_LVGL_QEMU_BOOT_SPLASH_BMP', None)
environment['CDK2_LVGL_RASTER_DIR'] = str(run / 'reference-raster')
(run / 'reference-raster').mkdir()
bmp_digest = ''
if b'CONFIG_USE_COREBOOT_FOR_BMP_RENDERING=y\n' in config:
    assert b'CONFIG_HAVE_CUSTOM_BMP_LOGO=y\n' not in config
    subprocess.run([str(producer / 'util/cbfstool/cbfstool'), str(producer / 'coreboot.rom'),
                    'extract', '-n', 'logo.bmp', '-f', str(run / 'logo.bmp')], check=True)
    environment['CDK2_LVGL_QEMU_BOOT_SPLASH_BMP'] = str(run / 'logo.bmp')
    bmp_digest = hashlib.sha256((run / 'logo.bmp').read_bytes()).hexdigest()
subprocess.run([str(stage / 'native/cdk2-lvgl-ui-driver-test')], env=environment, check=True)
(run / 'selecting-reference.ppm').write_bytes((run / 'reference-raster/qemu-profile-status-1.ppm').read_bytes())
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
manifest = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in fields.items()}
manifest.update(geometry=json.loads((run / 'reference-raster/qemu-profile-geometry.json').read_bytes()),
                ui_scale=0, lvgl_commit=actual, source_commit='d8e49af2ab0c90c6f6542b29026bd80309a9e1da',
                logo_bmp_sha256=bmp_digest)
(run / 'selecting-reference-manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
(stage / 'reference-config-before-after.json').write_text(json.dumps(before, indent=2, sort_keys=True) + '\n')
print('Actual normal628 reference raster and immutable source/config fields: PASS')
