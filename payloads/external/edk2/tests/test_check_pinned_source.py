#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).parents[1] / "check-pinned-source.sh"


def git(directory, *args, check=True):
    return subprocess.run(("git", "-C", directory, *args), check=check,
                          capture_output=True, text=True)


class CheckPinnedSourceTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.repo = self.directory.name
        git(self.repo, "init")
        git(self.repo, "config", "user.email", "test@example.com")
        git(self.repo, "config", "user.name", "Test")
        Path(self.repo, "tracked").write_text("first\n")
        Path(self.repo, "MdeModulePkg/Logo").mkdir(parents=True)
        Path(self.repo, "MdeModulePkg/Logo/Logo.bmp").write_text("first\n")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-m", "first")
        self.first = git(self.repo, "rev-parse", "HEAD").stdout.strip()
        Path(self.repo, "tracked").write_text("second\n")
        git(self.repo, "commit", "-am", "second")
        self.second = git(self.repo, "rev-parse", "HEAD").stdout.strip()

    def check_source(self, revision, allowed="", check=True):
        return subprocess.run((str(SCRIPT), self.repo, revision, allowed), check=check,
                              capture_output=True, text=True)

    def test_checks_out_clean_requested_commit(self):
        self.check_source(self.first)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD").stdout.strip(), self.first)

    def test_rejects_dirty_source_at_wrong_commit(self):
        Path(self.repo, "tracked").write_text("dirty\n")
        result = self.check_source(self.first, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tracked source is modified", result.stderr)

    def test_rejects_dirty_source_at_requested_commit(self):
        self.check_source(self.first)
        Path(self.repo, "tracked").write_text("dirty\n")
        result = self.check_source(self.first, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tracked source is modified", result.stderr)

    def test_rejects_non_commit_revision(self):
        result = self.check_source("not-a-commit", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a commit", result.stderr)

    def test_allows_only_the_configured_generated_logo(self):
        self.check_source(self.first)
        Path(self.repo, "MdeModulePkg/Logo/Logo.bmp").write_text("generated\n")
        self.check_source(self.first, "MdeModulePkg/Logo/Logo.bmp")

    def test_rejects_required_submodule_at_a_different_revision(self):
        module = Path(self.directory.name, "module")
        module.mkdir()
        git(str(module), "init")
        git(str(module), "config", "user.email", "test@example.com")
        git(str(module), "config", "user.name", "Test")
        (module / "file").write_text("first\n")
        git(str(module), "add", "file")
        git(str(module), "commit", "-m", "first")
        first = git(str(module), "rev-parse", "HEAD").stdout.strip()
        (module / "file").write_text("second\n")
        git(str(module), "commit", "-am", "second")
        environment = os.environ | {"GIT_ALLOW_PROTOCOL": "file"}
        subprocess.run(("git", "-C", self.repo, "submodule", "add", str(module),
                        "deps/module"), check=True, env=environment, capture_output=True)
        git(self.repo, "commit", "-am", "add module")
        requested = git(self.repo, "rev-parse", "HEAD").stdout.strip()
        git(str(Path(self.repo, "deps/module")), "checkout", "--detach", first)
        result = self.check_source(requested, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tracked source is modified", result.stderr)


if __name__ == "__main__":
    unittest.main()
