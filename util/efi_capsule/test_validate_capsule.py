#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Structural conversion checks; signature authorization is tested by the MM owner."""

import os
import struct
import subprocess
import tempfile
import unittest
import uuid
from pathlib import Path

import validate_capsule as validator


OLD_GUID = uuid.UUID('75b29ff6-e882-41c7-903f-5af89e004113')
NEW_GUID = uuid.UUID('eef5c7c3-124f-406a-87fe-4b58589fd331')


def volume(guids):
    data = bytearray(56)
    struct.pack_into('<Q4sIH', data, 32, 56 + 24 * len(guids), b'_FVH', 0, 56)
    for guid in guids:
        data += guid.bytes_le + b'\0\0\x07\0\x18\0\0\0'
    return bytes(data)


def capsule(firmware, target=OLD_GUID, embedded=b''):
    image = struct.pack('<4sIII', b'MSS1', 32, 0, 0) + target.bytes_le + firmware
    image = struct.pack('<I16sB3xIIQQ', 3, OLD_GUID.bytes_le, 1, len(image), 0, 0, 0) + image
    count = int(bool(embedded))
    offset = 8 + 8 * (count + 1)
    items = (struct.pack('<Q', offset) if count else b'')
    items += struct.pack('<Q', offset + len(embedded))
    body = struct.pack('<IHH', 1, count, 1) + items + embedded + image
    return struct.pack('<16sIII', validator.FMP_CAPSULE_GUID.bytes_le, 28, 0, 28 + len(body)) + body


class ConversionTest(unittest.TestCase):
    def setUp(self):
        self.layout = struct.pack('<8sBBQI32sH', b'__FMAP__', 1, 1, 0, 1024, b'FLASH', 1)
        self.layout += struct.pack('<II32sH', 0, 98, b'FMAP', 0)
        self.firmware = self.layout + b'\xff' * (1024 - len(self.layout))
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        output = Path(self.directory.name) / 'OUTPUT'
        output.mkdir()
        (output / 'FmpDxe.inf').write_text('[Defines]\nFILE_GUID = ' + str(OLD_GUID) + '\n')
        (output / 'FmpDxe.efi').write_bytes(b'embedded driver')

    def validate(self, guids=(NEW_GUID,), embedded=b'', **overrides):
        args = dict(capsule=capsule(self.firmware, embedded=embedded),
                    firmware_volume=volume(guids), expected_guid=OLD_GUID,
                    expected_embedded_count=int(bool(embedded)),
                    fmp_module_dir=self.directory.name,
                    expected_firmware=self.firmware, expected_layout=self.layout,
                    firmware_guid=NEW_GUID)
        args.update(overrides)
        validator.validate(**args)

    def test_conversion(self):
        self.validate()
        self.validate(embedded=b'embedded driver')

    def test_new_resident_is_required_even_with_embedded_driver(self):
        for guids in ((), (OLD_GUID,), (NEW_GUID, NEW_GUID)):
            with self.subTest(guids=guids), self.assertRaises(validator.ValidationError):
                self.validate(guids, embedded=b'embedded driver')

    def test_installed_identity_still_binds_capsule(self):
        with self.assertRaisesRegex(validator.ValidationError, 'authenticated capsule target'):
            self.validate(capsule=capsule(self.firmware, target=NEW_GUID))
        with self.assertRaisesRegex(validator.ValidationError, 'capsule image GUID'):
            self.validate(expected_guid=NEW_GUID)

    def test_complete_image_and_layout_are_required(self):
        for changes in (dict(expected_firmware=None), dict(expected_layout=None),
                        dict(expected_firmware=self.firmware[:-1]),
                        dict(expected_layout=self.layout[:56])):
            with self.subTest(changes=changes.keys()), self.assertRaises(validator.ValidationError):
                self.validate(**changes)

ROOT = Path(__file__).resolve().parents[2]


class BuildPolicyTest(unittest.TestCase):
    def test_payload_cannot_disable_the_required_profile(self):
        command = [
            'make', '--no-print-directory', 'print',
            'CONFIG_STARLABS_VBOOT_RELEASE_POLICY=y', 'CONFIG_VBOOT=y',
            'CONFIG_ASS=y', 'CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y',
            'CONFIG_EDK2_SECURE_BOOT_SUPPORT=y',
            'CONFIG_DRIVERS_EFI_MAIN_FW_GUID=' + str(NEW_GUID),
            'CONFIG_DRIVERS_EFI_CAPSULE_REGIONS=EC RW_NVRAM FW_MAIN_A RW_FWID_A VBLOCK_A',
            'CONFIG_LOCALVERSION=26.12',
            'CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=0x00000000',
            'CONFIG_DRIVERS_EFI_MAIN_FW_LSV=0x00000000',
        ]
        directory = ROOT / 'payloads/external/edk2'
        control = subprocess.run(command, cwd=directory, capture_output=True, text=True)
        self.assertEqual(control.returncode, 0, control.stdout + control.stderr)
        self.assertIn('SECURE_BOOT_ENABLE=TRUE', control.stdout)
        overrides = [
            'CONFIG_EDK2_SECURE_BOOT_SUPPORT=n',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=-D SECURE_BOOT_ENABLE=FALSE',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=-DSECURE_BOOT_ENABLE=FALSE',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=-D CAPSULE_SUPPORT=FALSE',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=-D CAPSULE_MAIN_FW_GUID=' + str(OLD_GUID),
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=-D FMP_DEVICE_PKCS7_PCD_INC=other.inc',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=--pcd '
            'gFmpDevicePkgTokenSpaceGuid.PcdFmpDevicePkcs7CertBufferXdr={0}',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=--define="SECURE_BOOT_ENABLE=FALSE"',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=--pcd '
            'gEfiSecurityPkgTokenSpaceGuid.PcdRemovableMediaImageVerificationPolicy=0x00',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=--pcd '
            'gEfiSecurityPkgTokenSpaceGuid.PcdFixedMediaImageVerificationPolicy=0x00',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=--pcd='
            'gEfiSecurityPkgTokenSpaceGuid.PcdOptionRomImageVerificationPolicy=0x00',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=--pcd '
            'gUefiPayloadPkgTokenSpaceGuid.PcdDefaultSecureBootEnable=FALSE',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=--pcd='
            'gEfiMdeModulePkgTokenSpaceGuid.PcdTpm2MeasurementRequired=FALSE',
        ]
        for override in overrides:
            with self.subTest(override=override):
                result = subprocess.run(command + [override], cwd=directory,
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('Star Labs VBOOT', result.stderr)

    def test_fixed_mm_required_options_follow_custom_options(self):
        result = subprocess.run([
            'make', '--no-print-directory', '-n', '-o', 'prep', '-o', 'print',
            'FixedMmService', 'CONFIG_STARLABS_VBOOT_RELEASE_POLICY=y',
            'CONFIG_VBOOT=y', 'CONFIG_ASS=y',
            'CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y',
            'CONFIG_EDK2_SECURE_BOOT_SUPPORT=y',
            'CONFIG_DRIVERS_EFI_MAIN_FW_GUID=' + str(NEW_GUID),
            'CONFIG_DRIVERS_EFI_CAPSULE_REGIONS=EC RW_NVRAM FW_MAIN_A RW_FWID_A VBLOCK_A',
            'CONFIG_LOCALVERSION=26.12',
            'CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=0x00000000',
            'CONFIG_DRIVERS_EFI_MAIN_FW_LSV=0x00000000',
            'CONFIG_EDK2_CUSTOM_BUILD_PARAMS=--def=CAPSULE_SUPPORT=FALSE',
        ], cwd=ROOT / 'payloads/external/edk2', capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        command = result.stdout.split('build -a IA32', 1)[1]
        self.assertGreater(command.rindex('-D CAPSULE_SUPPORT=TRUE'),
                           command.index('--def=CAPSULE_SUPPORT=FALSE'))
        self.assertGreater(command.rindex('-D FMP_DEVICE_PKCS7_PCD_INC='),
                           command.index('--def=CAPSULE_SUPPORT=FALSE'))

    def test_repository_preparation_cannot_modify_its_parent_repository(self):
        environment = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull,
                           GIT_CONFIG_NOSYSTEM='1', GIT_ALLOW_PROTOCOL='file', LC_ALL='C')
        with tempfile.TemporaryDirectory(prefix='edk-source-', dir='/tmp') as source_name:
            source = Path(source_name)

            def git(directory, *arguments):
                result = subprocess.run([
                    'git', '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid',
                    '-C', str(directory), *arguments,
                ], env=environment, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result.stdout.strip()

            git(source, 'init', '-q')
            (source / 'source').write_text('payload source')
            git(source, 'add', 'source')
            git(source, 'commit', '-qm', 'source')
            revision = git(source, 'rev-parse', 'HEAD')
            for mode in ['short-path', 'not-a-repository', 'file-url']:
                with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                    parent = Path(directory)
                    git(parent, 'init', '-q')
                    (parent / '.gitignore').write_text('caller/\n')
                    git(parent, 'add', '.gitignore')
                    git(parent, 'commit', '-qm', 'parent')
                    git(parent, 'remote', 'add', 'origin', parent.as_uri())
                    head = git(parent, 'rev-parse', 'HEAD')
                    caller = parent / 'caller'
                    workspace = caller / 'workspace'
                    workspace.mkdir(parents=True)
                    repository = str(source) if mode == 'short-path' else source.as_uri()
                    target = workspace if mode == 'short-path' else workspace / source.name
                    if mode == 'not-a-repository':
                        target.mkdir()
                    result = subprocess.run([
                        'make', '--no-print-directory', '-f',
                        str(ROOT / 'payloads/external/edk2/Makefile'),
                        str(target) + ('/' if mode == 'short-path' else ''),
                        'CONFIG_EDK2_REPOSITORY=' + repository,
                        'CONFIG_EDK2_TAG_OR_REV=' + revision,
                    ], cwd=caller, env=environment, capture_output=True, text=True)
                    if mode == 'file-url':
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertEqual(git(target, 'rev-parse', 'HEAD'), revision)
                    else:
                        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(git(parent, 'rev-parse', 'HEAD'), head)
                    self.assertEqual(git(parent, 'remote', 'get-url', 'origin'), parent.as_uri())

    def test_capsule_request_changes_cannot_reuse_an_old_capsule(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            output = path / 'build'
            output.mkdir()
            rom = output / 'coreboot.rom'
            rom.write_bytes(b'fixture ROM')
            config = path / 'config'
            config.write_text('')
            recorder = path / 'record.py'
            recorder.write_text('''#!/usr/bin/env python3
import sys
from pathlib import Path
args = dict(item.split('=', 1) for item in sys.argv[1:] if '=' in item)
out = Path(args['COREBOOT_CAPSULE_OUT'])
with out.with_suffix('.calls').open('a') as calls:
    calls.write('called\\n')
out.write_text(args.get('CAPSULE_LAYOUT_TRANSITION', '0') + ':' +
               args.get('CAPSULE_GUID', args['FIRMWARE_GUID']))
''')
            recorder.chmod(0o755)
            makefile = path / 'Makefile'
            makefile.write_text(f'''top := {ROOT}
obj := {output}
DOTCONFIG := {config}
NOCOMPILE := 1
KERNELVERSION := fixture
strip_quotes = $(subst ",,$(1))
CONFIG_PAYLOAD_EDK2 := y
CONFIG_DRIVERS_EFI_UPDATE_CAPSULES := y
CONFIG_DRIVERS_EFI_GENERATE_CAPSULE := y
CONFIG_STARLABS_VBOOT_RELEASE_POLICY := y
CONFIG_VBOOT := y
CONFIG_DRIVERS_EFI_MAIN_FW_GUID := {NEW_GUID}
include {ROOT}/Makefile.mk
include {ROOT}/payloads/external/Makefile.mk
''')
            for mode, guid in [('0', NEW_GUID), ('1', OLD_GUID), ('0', NEW_GUID)]:
                with self.subTest(mode=mode, guid=guid):
                    # Firmware compilation and signing are fixture boundaries;
                    # the production parent rule and argument expansion run unchanged.
                    result = subprocess.run([
                        'make', '--no-print-directory', '-f', str(makefile),
                        '-o', str(rom), '-o', 'coreboot', '-o', 'build_complete',
                        '-o', 'files_added', 'capsule', 'MAKE=' + str(recorder),
                        'CAPSULE_LAYOUT_TRANSITION=' + mode, 'CAPSULE_GUID=' + str(guid),
                    ], cwd=ROOT, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual((output / 'coreboot.cap').read_text(),
                                     mode + ':' + str(guid))
            self.assertEqual((output / 'coreboot.calls').read_text().splitlines(),
                             ['called'] * 3)


if __name__ == '__main__':
    unittest.main()
