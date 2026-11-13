#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise source admission against disposable Git repos and nested submodules."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PINNED = ROOT / "payloads/external/edk2/check-pinned-source.sh"
DAVE = ROOT / "payloads/external/edk2/check-dave-source.sh"


class SourceAdmission(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="dave-source-test-")
        self.addCleanup(self.temporary.cleanup)
        self.top = Path(self.temporary.name)
        self.env = dict(os.environ, GIT_ALLOW_PROTOCOL="file",
                        GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL="/dev/null")
        self.leaf = self.make_repo("leaf")
        (self.leaf / "nested.c").write_text("nested\n")
        self.commit(self.leaf, "leaf")
        self.child = self.make_repo("child")
        self.git(self.child, "submodule", "add", "-q", str(self.leaf), "nested")
        (self.child / "child.c").write_text("child\n")
        self.commit(self.child, "child")
        self.source = self.make_repo("source")
        self.git(self.source, "submodule", "add", "-q", str(self.child), "sub dir")
        self.git(self.source, "submodule", "update", "--init", "--recursive", "-q")
        dsc = self.source / "UefiPayloadPkg"
        dsc.mkdir()
        (dsc / "UefiPayloadPkg.dsc").write_text(
            "DEFINE STARLABS_DAVE = FALSE\nDEFINE COREBOOT_DAVE_VBOOT = FALSE\n"
            "DEFINE COREBOOT_DAVE_SRK_PIN = FALSE\n")
        (dsc / "CorebootFixedMmService.dsc").write_text(
            "DEFINE STARLABS_DAVE = FALSE\nDEFINE STATIC_SECURE_BOOT = FALSE\n"
            "DEFINE COREBOOT_DAVE_VBOOT = FALSE\nDEFINE COREBOOT_DAVE_SRK_PIN = FALSE\n")
        (self.source / "generated logo.bmp").write_text("original\n")
        (self.source / "source.c").write_text("original\n")
        (self.source / ".gitignore").write_text("Build/\n")
        self.commit(self.source, "source")
        self.revision = self.git(self.source, "rev-parse", "HEAD").stdout.strip()

    def make_repo(self, name):
        path = self.top / name
        path.mkdir()
        self.git(path, "init", "-q")
        self.git(path, "config", "user.name", "Fixture")
        self.git(path, "config", "user.email", "fixture@example.invalid")
        return path

    def git(self, path, *args):
        result = subprocess.run(["git", *args], cwd=path, env=self.env,
                                capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(f"git {args!r}: {result.stderr}")
        return result

    def commit(self, path, message):
        self.git(path, "add", "--all")
        self.git(path, "commit", "-qm", message)

    def pinned(self, env=None):
        return subprocess.run([str(PINNED), str(self.source), self.revision,
                               "generated logo.bmp"], env=env or self.env,
                              text=True, capture_output=True)

    def dave(self, env=None):
        return subprocess.run([str(DAVE), str(self.source), "y"], env=env or self.env,
                              text=True, capture_output=True)

    def test_clean_and_exact_tracked_asset_exception(self):
        self.assertEqual(self.pinned().returncode, 0)
        (self.source / "generated logo.bmp").write_text("generated\n")
        self.assertEqual(self.pinned().returncode, 0)
        self.assertEqual(self.dave().returncode, 0)

    def test_tracked_source_change_is_rejected(self):
        (self.source / "source.c").write_text("changed\n")
        self.assertNotEqual(self.pinned().returncode, 0)

    def test_untracked_main_tree_is_rejected_without_deletion(self):
        extra = self.source / "header with\nnewline.h"
        extra.write_text("unreviewed\n")
        self.assertNotEqual(self.dave().returncode, 0)
        self.assertEqual(extra.read_text(), "unreviewed\n")

    def test_untracked_nested_submodule_is_rejected(self):
        extra = self.source / "sub dir/nested/unreviewed header.h"
        extra.write_text("unreviewed\n")
        self.assertNotEqual(self.dave().returncode, 0)
        self.assertTrue(extra.exists())

    def test_untracked_respected_despite_submodule_ignore_setting(self):
        self.git(self.source, "config", "submodule.sub dir.ignore", "untracked")
        extra = self.source / "sub dir/unreviewed.c"
        extra.write_text("unreviewed\n")
        self.assertNotEqual(self.dave().returncode, 0)
        self.assertTrue(extra.exists())

    def test_ignored_output_is_not_claimed_checked(self):
        (self.source / "Build").mkdir()
        (self.source / "Build/generated.h").write_text("ignored output\n")
        self.assertEqual(self.dave().returncode, 0)

    def test_submodule_status_command_error_is_rejected(self):
        fakebin = self.top / "bin"
        fakebin.mkdir()
        realgit = subprocess.run(["which", "git"], capture_output=True, text=True,
                                 check=True).stdout.strip()
        shim = fakebin / "git"
        shim.write_text("#!/bin/sh\nif [ \"$1\" = submodule ] && [ \"${2:-}\" = status ]; "
                        "then exit 42; fi\nexec \"" + realgit + "\" \"$@\"\n")
        shim.chmod(0o755)
        env = dict(self.env, PATH=f"{fakebin}:{self.env['PATH']}")
        result = self.pinned(env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unable to inspect submodule revisions", result.stderr)

    def test_untracked_scan_error_is_rejected(self):
        fakebin = self.top / "bin"
        fakebin.mkdir()
        realgit = subprocess.run(["which", "git"], capture_output=True, text=True,
                                 check=True).stdout.strip()
        shim = fakebin / "git"
        shim.write_text("#!/bin/sh\nif [ \"$1\" = ls-files ]; "
                        "then exit 42; fi\nexec \"" + realgit + "\" \"$@\"\n")
        shim.chmod(0o755)
        env = dict(self.env, PATH=f"{fakebin}:{self.env['PATH']}")
        result = self.dave(env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unable to establish a clean source tree", result.stderr)

    def test_git_diff_error_is_not_treated_as_a_clean_tree(self):
        fakebin = self.top / "bin"
        fakebin.mkdir()
        realgit = subprocess.run(["which", "git"], capture_output=True, text=True,
                                 check=True).stdout.strip()
        shim = fakebin / "git"
        shim.write_text("#!/bin/sh\nif [ \"$1\" = diff ]; "
                        "then exit 42; fi\nexec \"" + realgit + "\" \"$@\"\n")
        shim.chmod(0o755)
        env = dict(self.env, PATH=f"{fakebin}:{self.env['PATH']}")
        result = self.pinned(env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unable to inspect tracked source", result.stderr)

    def test_scan_setup_failure_is_rejected(self):
        env = dict(self.env, TMPDIR=str(self.top / "missing-directory"))
        result = self.dave(env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unable to establish a clean source tree", result.stderr)

    def test_missing_source_is_rejected(self):
        result = subprocess.run([str(DAVE), str(self.top / "missing-source"), "y"],
                                env=self.env, text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unable to establish a clean source tree", result.stderr)

    def test_missing_required_contract_is_rejected(self):
        (self.source / "UefiPayloadPkg/CorebootFixedMmService.dsc").write_text("DEFINE STARLABS_DAVE = FALSE\n")
        self.assertNotEqual(self.dave().returncode, 0)


if __name__ == "__main__":
    unittest.main()
