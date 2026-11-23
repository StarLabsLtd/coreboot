#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Exercise offline EDK2 preparation through the payload and capsule targets."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_validate_capsule import OLD_GUID, capsule, volume


ROOT = Path(__file__).resolve().parents[2]
EDK2 = ROOT / 'payloads/external/edk2'


class CachedCheckoutTest(unittest.TestCase):
    def git(self, directory, *args):
        return subprocess.check_output(
            ['git', '-C', str(directory), *args], env=self.online, text=True).strip()

    def repository(self, name):
        repo = self.directory / name
        repo.mkdir()
        self.git(repo, 'init', '-q', '-b', 'main')
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

        self.repo = repo
        (repo / 'edksetup.sh').write_text(':\n')
        (repo / 'BaseTools').mkdir()
        (repo / 'BaseTools/Makefile').write_text('all:\n\ttest -f ../dependency/file\n')
        tools = repo / 'BaseTools/BinWrappers/PosixLike'
        tools.mkdir(parents=True)
        generator = tools / 'GenerateCapsule'
        generator.write_text('''#!/bin/sh
set -e
[ "$1" = -e ] && [ "$2" = -o ] || exit 1
output=$3
for input; do :; done
if [ -n "${TEST_CAPSULE_INPUT:-}" ]; then
    cp "$input" "$TEST_CAPSULE_INPUT"
fi
cp "$TEST_CAPSULE" "$output"
''')
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
        initial = self.make('prepare-source', self.online)
        self.assertEqual(initial.returncode, 0, initial.stdout + initial.stderr)
        producer_index = self.directory / 'producer.index'
        env = dict(self.offline, GIT_INDEX_FILE=str(producer_index))
        subprocess.run(['git', 'read-tree', 'HEAD'], cwd=ROOT, env=env, check=True)
        original_index = producer_index.read_bytes()
        prepared = self.make('prep', env)
        self.assertEqual(prepared.returncode, 0, prepared.stdout + prepared.stderr)
        self.assertEqual(producer_index.read_bytes(), original_index)

        fv = self.workspace / 'Build/UefiPayloadPkgX64/RELEASE_GCC/FV/DXEFV.Fv'
        fv.parent.mkdir(parents=True)
        fv.write_bytes(volume([OLD_GUID]))
        packaged = self.make('coreboot_capsule', self.offline)
        self.assertEqual(packaged.returncode, 0, packaged.stdout + packaged.stderr)
        self.assertEqual((self.directory / 'coreboot.cap').read_bytes(), self.fixture.read_bytes())

    def test_branch_refresh_changes_source_state_only_when_head_changes(self):
        config = dict(CONFIG_EDK2_TAG_OR_REV='origin/main')
        state = self.workspace / '.coreboot-source'
        initial = self.make('prepare-source', self.online, **config)
        self.assertEqual(initial.returncode, 0, initial.stdout + initial.stderr)
        self.assertEqual(state.read_text().strip(), self.pin)
        initial_mtime = state.stat().st_mtime_ns

        (self.repo / 'edksetup.sh').write_text('# updated fixture source\n')
        self.git(self.repo, 'add', 'edksetup.sh')
        self.commit(self.repo)
        new = self.git(self.repo, 'rev-parse', 'HEAD')
        verified = self.make(str(self.checkout), self.offline, **config)
        self.assertEqual(verified.returncode, 0, verified.stdout + verified.stderr)
        self.assertEqual(self.git(self.checkout, 'rev-parse', 'HEAD'), self.pin)
        self.assertEqual(state.stat().st_mtime_ns, initial_mtime)

        refreshed = self.make('prepare-source', self.online, **config)
        self.assertEqual(refreshed.returncode, 0, refreshed.stdout + refreshed.stderr)
        self.assertEqual(state.read_text().strip(), new)
        refreshed_mtime = state.stat().st_mtime_ns
        self.assertGreater(refreshed_mtime, initial_mtime)
        unchanged = self.make('prepare-source', self.online, **config)
        self.assertEqual(unchanged.returncode, 0, unchanged.stdout + unchanged.stderr)
        self.assertEqual(state.stat().st_mtime_ns, refreshed_mtime)

    def test_remote_head_tracks_the_current_default_branch(self):
        config = dict(CONFIG_EDK2_TAG_OR_REV='origin/HEAD')
        initial = self.make('prepare-source', self.online, **config)
        self.assertEqual(initial.returncode, 0, initial.stdout + initial.stderr)
        self.git(self.repo, 'checkout', '-b', 'new-default')
        (self.repo / 'edksetup.sh').write_text('# new default branch\n')
        self.git(self.repo, 'add', 'edksetup.sh')
        self.commit(self.repo)
        expected = self.git(self.repo, 'rev-parse', 'HEAD')

        refreshed = self.make('prepare-source', self.online, **config)
        self.assertEqual(refreshed.returncode, 0, refreshed.stdout + refreshed.stderr)
        self.assertEqual(self.git(self.checkout, 'rev-parse', 'HEAD'), expected)
        verified = self.make('verify-source', self.offline, **config)
        self.assertEqual(verified.returncode, 0, verified.stdout + verified.stderr)

    def test_deleted_branch_cannot_be_prepared_from_cached_ref(self):
        branch = 'release-candidate'
        self.git(self.repo, 'branch', branch)
        config = dict(CONFIG_EDK2_TAG_OR_REV='origin/' + branch)
        initial = self.make('prepare-source', self.online, **config)
        self.assertEqual(initial.returncode, 0, initial.stdout + initial.stderr)
        self.assertEqual(self.git(self.checkout, 'rev-parse', 'origin/' + branch), self.pin)

        self.git(self.repo, 'branch', '-d', branch)
        state = self.workspace / '.coreboot-source'
        before = state.read_bytes()
        rejected = self.make('prepare-source', self.online, **config)
        self.assertNotEqual(rejected.returncode, 0, rejected.stdout + rejected.stderr)
        self.assertEqual(self.git(self.checkout, 'rev-parse', 'HEAD'), self.pin)
        self.assertEqual(state.read_bytes(), before)
        verified = self.make('verify-source', self.offline, **config)
        self.assertEqual(verified.returncode, 0, verified.stdout + verified.stderr)

    def test_release_tag_prepares_and_verifies_offline(self):
        initial = self.make('prepare-source', self.online)
        self.assertEqual(initial.returncode, 0, initial.stdout + initial.stderr)
        self.git(self.repo, 'checkout', '--detach')
        (self.repo / 'edksetup.sh').write_text('# release source\n')
        self.git(self.repo, 'add', 'edksetup.sh')
        self.commit(self.repo)
        release = self.git(self.repo, 'rev-parse', 'HEAD')
        self.git(self.repo, '-c', 'user.email=test@example.com', '-c', 'user.name=Test',
                 'tag', '-a', 'release-test', '-m', 'Test release')
        self.git(self.repo, 'checkout', '-')
        (self.repo / 'edksetup.sh').write_text('# newer development source\n')
        self.git(self.repo, 'add', 'edksetup.sh')
        self.commit(self.repo)

        config = dict(CONFIG_EDK2_TAG_OR_REV='refs/tags/release-test')
        prepared = self.make('prepare-source', self.online, **config)
        self.assertEqual(prepared.returncode, 0, prepared.stdout + prepared.stderr)
        verified = self.make('verify-source', self.offline, **config)
        self.assertEqual(verified.returncode, 0, verified.stdout + verified.stderr)
        self.assertEqual(self.git(self.checkout, 'rev-parse', 'HEAD'), release)
        self.assertEqual((self.workspace / '.coreboot-source').read_text().strip(), release)

    def test_outer_capsule_requires_manifest_for_vboot(self):
        fv = self.workspace / 'Build/UefiPayloadPkgX64/RELEASE_GCC/FV/DXEFV.Fv'
        fv.parent.mkdir(parents=True)
        fv.write_bytes(volume([OLD_GUID]))
        config = self.directory / 'config'
        makefile = self.directory / 'Makefile'
        config.write_text('')
        generator_input = self.directory / 'generator-input'
        output = self.directory / 'coreboot.cap'
        env = dict(self.online, TEST_CAPSULE=str(self.fixture),
                   TEST_CAPSULE_INPUT=str(generator_input))
        env.pop('CONFIG_VBOOT', None)

        for vboot, manifest in (('y', False), ('', False), ('y', True)):
            with self.subTest(vboot=vboot, manifest=manifest):
                if manifest:
                    helper = self.repo / 'BaseTools/Scripts/AppendRmapManifest.py'
                    helper.parent.mkdir()
                    helper.write_text('''import sys
from pathlib import Path

assert sys.argv[1] == '-o'
assert sys.argv[3:5] == ['-r', 'COREBOOT']
Path(sys.argv[2]).write_bytes(Path(sys.argv[5]).read_bytes() + b'manifest')
''')
                    self.git(self.repo, 'add', str(helper))
                    self.commit(self.repo)
                    self.pin = self.git(self.repo, 'rev-parse', 'HEAD')

                output.unlink(missing_ok=True)
                generator_input.unlink(missing_ok=True)
                # Keep CONFIG_VBOOT in the parent so MAKEFLAGS cannot forward it.
                makefile.write_text(f'''top := {ROOT}
obj := {self.directory}
DOTCONFIG := {config}
NOCOMPILE := 1
KERNELVERSION := fixture
strip_quotes = $(subst ",,$(1))
CONFIG_PAYLOAD_EDK2 := y
CONFIG_DRIVERS_EFI_UPDATE_CAPSULES := y
CONFIG_DRIVERS_EFI_GENERATE_CAPSULE := y
CONFIG_STARLABS_VBOOT_RELEASE_POLICY := n
CONFIG_VBOOT := {vboot}
CONFIG_EDK2_REPOSITORY := {self.repository_url}
CONFIG_EDK2_TAG_OR_REV := {self.pin}
CONFIG_DRIVERS_EFI_MAIN_FW_GUID := {OLD_GUID}
CONFIG_DRIVERS_EFI_MAIN_FW_VERSION := 0x10000
CONFIG_DRIVERS_EFI_MAIN_FW_LSV := 0
CONFIG_DRIVERS_EFI_CAPSULE_REGIONS := COREBOOT
CONFIG_DRIVERS_EFI_CAPSULE_SIGNER_PRIVATE_CERT := edksetup.sh
CONFIG_DRIVERS_EFI_CAPSULE_OTHER_PUBLIC_CERT := edksetup.sh
CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT := edksetup.sh
include {ROOT}/Makefile.mk
include {ROOT}/payloads/external/Makefile.mk
''')
                prepared = self.make('prepare-source', self.online)
                self.assertEqual(prepared.returncode, 0, prepared.stdout + prepared.stderr)
                result = subprocess.run([
                    'make', '--no-print-directory', '-f', str(makefile),
                    '-o', str(self.rom), '-o', 'coreboot', '-o', 'build_complete',
                    '-o', 'files_added', 'capsule', f'WORKSPACE={self.workspace}',
                    f'EDK2_PATH={self.checkout}',
                ], cwd=ROOT, env=env, capture_output=True, text=True)
                diagnostic = result.stdout + result.stderr
                if vboot and not manifest:
                    self.assertNotEqual(result.returncode, 0, diagnostic)
                    self.assertFalse(output.exists())
                    self.assertFalse(generator_input.exists())
                else:
                    self.assertEqual(result.returncode, 0, diagnostic)
                    self.assertEqual(output.read_bytes(), self.fixture.read_bytes())
                    expected = self.rom.read_bytes() + (b'manifest' if manifest else b'')
                    self.assertEqual(generator_input.read_bytes(), expected)

    def test_ass_payload_uses_only_client_sources(self):
        prepared = self.make('prepare-source', self.online)
        self.assertEqual(prepared.returncode, 0, prepared.stdout + prepared.stderr)
        result = self.make('prep', self.online, CONFIG_ASS='y')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_submodule_fails_offline(self):
        self.workspace.mkdir()
        self.git(self.workspace, 'clone', '-q', self.repository_url, str(self.checkout))
        result = self.make('prep', self.offline)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('before building', result.stderr)

    def test_failed_offline_switch_can_be_retried_online(self):
        initial = self.make('prepare-source', self.online)
        self.assertEqual(initial.returncode, 0, initial.stdout + initial.stderr)
        dependency = self.directory / 'dependency'
        (dependency / 'file').write_text('new dependency revision\n')
        self.git(dependency, 'add', 'file')
        self.commit(dependency)
        self.git(self.repo, 'submodule', 'update', '--remote', 'dependency')
        self.git(self.repo, 'add', 'dependency')
        self.commit(self.repo)
        next_pin = self.git(self.repo, 'rev-parse', 'HEAD')
        self.git(self.checkout, 'fetch', '--no-recurse-submodules', 'origin')

        overrides = dict(CONFIG_EDK2_TAG_OR_REV=next_pin)
        attempt = self.make('prepare-source', self.offline, **overrides)
        self.assertNotEqual(attempt.returncode, 0, attempt.stdout + attempt.stderr)
        self.assertEqual(self.git(self.checkout, 'rev-parse', 'HEAD'), self.pin)
        retry = self.make('prepare-source', self.online, **overrides)
        self.assertEqual(retry.returncode, 0, retry.stdout + retry.stderr)
        self.assertEqual(self.git(self.checkout, 'rev-parse', 'HEAD'), next_pin)

    def test_wrong_origin_unavailable_pin_and_dirty_source_fail(self):
        initial = self.make('prepare-source', self.online)
        self.assertEqual(initial.returncode, 0, initial.stdout + initial.stderr)
        for override, diagnostic in (
                ({'CONFIG_EDK2_REPOSITORY': self.repository('wrong').as_uri()},
                 'belongs to a different repository'),
                ({'CONFIG_EDK2_TAG_OR_REV': '0' * 40}, 'before building')):
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
