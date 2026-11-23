#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Keep payload preparation confined to its own source checkout."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class SourcePreparationTest(unittest.TestCase):
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
                    command = [
                        'make', '--no-print-directory', '-f',
                        str(ROOT / 'payloads/external/edk2/Makefile'),
                        'CONFIG_EDK2_REPOSITORY=' + repository,
                        'CONFIG_EDK2_TAG_OR_REV=' + revision,
                    ]
                    if mode == 'file-url':
                        unprepared = subprocess.run(command + [str(target)], cwd=caller,
                                                    env=environment, capture_output=True,
                                                    text=True)
                        self.assertNotEqual(unprepared.returncode, 0)
                        self.assertIn('before building', unprepared.stderr)
                        self.assertFalse(target.exists())
                    result = subprocess.run(command + ['prepare-source'],
                                            cwd=caller, env=environment,
                                            capture_output=True, text=True)
                    if mode == 'file-url':
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertEqual(git(target, 'rev-parse', 'HEAD'), revision)
                        result = subprocess.run(command + ['-B', str(target)], cwd=caller,
                                                env=environment, capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    else:
                        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(git(parent, 'rev-parse', 'HEAD'), head)
                    self.assertEqual(git(parent, 'remote', 'get-url', 'origin'), parent.as_uri())


if __name__ == '__main__':
    unittest.main()
