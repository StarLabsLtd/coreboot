#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Exercise offline EDK2 preparation through the payload and capsule targets."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_validate_capsule import OLD_GUID, capsule, volume


EDK2 = Path(__file__).resolve().parents[2] / 'payloads/external/edk2'


class CachedCheckoutTest(unittest.TestCase):
    def git(self, directory, *args):
        return subprocess.check_output(
            ['git', '-C', str(directory), *args], env=self.online, text=True).strip()

    def repository(self, name):
        repo = self.directory / name
        repo.mkdir()
        self.git(repo, 'init', '-q')
        (repo / 'file').touch()
        self.git(repo, 'add', 'file')
        self.commit(repo)
        return repo

    def commit(self, repo):
        self.git(repo, '-c', 'user.email=test@example.com', '-c', 'user.name=Test',
                 'commit', '-qm', 'Test source')

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)
        self.online = dict(os.environ, GIT_ALLOW_PROTOCOL='file')
        self.offline = dict(os.environ, GIT_ALLOW_PROTOCOL='')
        dependency = self.repository('dependency')
        repo = self.repository('source')
        self.git(repo, 'submodule', 'add', '-q', dependency.as_uri(), 'dependency')

        (repo / 'edksetup.sh').write_text(':\n')
        (repo / 'BaseTools').mkdir()
        (repo / 'BaseTools/Makefile').write_text('all:\n\ttest -f ../dependency/file\n')
        tools = repo / 'BaseTools/BinWrappers/PosixLike'
        tools.mkdir(parents=True)
        generator = tools / 'GenerateCapsule'
        generator.write_text('#!/bin/sh\n[ "$1" = -e ] && [ "$2" = -o ] && cp "$TEST_CAPSULE" "$3"\n')
        generator.chmod(0o755)
        self.git(repo, 'add', '.')
        self.commit(repo)

        self.workspace = self.directory / 'workspace'
        self.checkout = self.workspace / 'source'
        self.repository_url = repo.as_uri()
        self.pin = self.git(repo, 'rev-parse', 'HEAD')
        self.rom = self.directory / 'coreboot.rom'
        self.rom.write_bytes(b'fake ROM')
        self.fixture = self.directory / 'fixture.cap'
        self.fixture.write_bytes(capsule(self.rom.read_bytes()))
        self.offline['TEST_CAPSULE'] = str(self.fixture)

    def make(self, target, env, **overrides):
        config = {
            'HOSTCC': 'cc',
            'WORKSPACE': self.workspace,
            'EDK2_PATH': self.checkout,
            'CONFIG_EDK2_REPOSITORY': self.repository_url,
            'CONFIG_EDK2_TAG_OR_REV': self.pin,
            'CONFIG_DRIVERS_EFI_MAIN_FW_GUID': OLD_GUID,
            'CONFIG_DRIVERS_EFI_MAIN_FW_VERSION': '0x10000',
            'CONFIG_DRIVERS_EFI_MAIN_FW_LSV': '0',
            'CONFIG_DRIVERS_EFI_CAPSULE_SIGNER_PRIVATE_CERT': 'edksetup.sh',
            'CONFIG_DRIVERS_EFI_CAPSULE_OTHER_PUBLIC_CERT': 'edksetup.sh',
            'CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT': 'edksetup.sh',
            'COREBOOT_ROM': self.rom,
            'COREBOOT_CAPSULE_OUT': self.directory / 'coreboot.cap',
        }
        config.update(overrides)
        return subprocess.run(['make', '--no-print-directory', target] +
                              [f'{key}={value}' for key, value in config.items()],
                              cwd=EDK2, env=env, capture_output=True, text=True)

    def test_cached_pin_prepares_and_packages_offline(self):
        initial = self.make(str(self.checkout), self.online)
        self.assertEqual(initial.returncode, 0, initial.stdout + initial.stderr)
        prepared = self.make('prep', self.offline)
        self.assertEqual(prepared.returncode, 0, prepared.stdout + prepared.stderr)

        fv = self.workspace / 'Build/UefiPayloadPkgX64/RELEASE_GCC/FV/DXEFV.Fv'
        fv.parent.mkdir(parents=True)
        fv.write_bytes(volume([OLD_GUID]))
        packaged = self.make('coreboot_capsule', self.offline)
        self.assertEqual(packaged.returncode, 0, packaged.stdout + packaged.stderr)
        self.assertEqual((self.directory / 'coreboot.cap').read_bytes(), self.fixture.read_bytes())

    def test_missing_submodule_fails_offline(self):
        self.workspace.mkdir()
        self.git(self.workspace, 'clone', '-q', self.repository_url, str(self.checkout))
        result = self.make('prep', self.offline)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('transport', result.stderr)

    def test_wrong_origin_unavailable_pin_and_dirty_source_fail(self):
        initial = self.make(str(self.checkout), self.online)
        self.assertEqual(initial.returncode, 0, initial.stdout + initial.stderr)
        for override, diagnostic in (
                ({'CONFIG_EDK2_REPOSITORY': self.repository('wrong').as_uri()},
                 'belongs to a different repository'),
                ({'CONFIG_EDK2_TAG_OR_REV': '0' * 40}, 'transport')):
            result = self.make('prep', self.offline, **override)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(diagnostic, result.stderr)
        for path in ('edksetup.sh', 'dependency/file'):
            file = self.checkout / path
            file.write_text('local change\n')
            result = self.make('prep', self.offline)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('tracked changes', result.stderr)
            self.assertEqual(file.read_text(), 'local change\n')
            self.git(file.parent, 'checkout', '--', file.name)


if __name__ == '__main__':
    unittest.main()
