#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""HOST model for the real outer Make hook, not firmware/CBFS validation."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class PinnedMakeFrontdoor(unittest.TestCase):
    """Real pinned Make/config/scanner; no vendor init or firmware build.

    CDK2_TEST_SOURCE_REPO names a local object store containing the gitlink.
    Only the five required source blobs are materialised in an isolated temp.
    """

    def setUp(self):
        repository = os.environ.get("CDK2_TEST_SOURCE_REPO")
        if not repository:
            self.skipTest("set CDK2_TEST_SOURCE_REPO for actual pinned-source HOST checks")
        entry = subprocess.check_output(
            ["git", "-C", str(ROOT), "ls-tree", "HEAD", "payloads/external/cdk2/cdk2"],
            text=True,
        ).split()
        self.assertEqual(entry[:2], ["160000", "commit"])
        self.temporary = tempfile.TemporaryDirectory(prefix="cdk2-pinned-scanner-")
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name)
        for relative in ("Makefile", "Kconfig", "defconfig", "src/boot/Makefile",
                         "util/strict-direct-cbfs-raw-envelope.c"):
            body = subprocess.check_output(
                ["git", "-C", repository, "show", f"{entry[2]}:{relative}"])
            path = self.source / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(body)
        self.build = self.source / "output"
        self.scanner = self.build / "native/strict-direct-cbfs-raw-envelope"
        self.environment = os.environ.copy()
        for name in tuple(self.environment):
            if name.startswith("CDK2_") or name in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES",
                                                   "COREBOOT_CONFIG", "COREBOOT_OUTPUT_DIR"):
                self.environment.pop(name, None)
        self.make = shutil.which("make")
        self.conf = shutil.which("kconfig-conf")
        self.compiler = shutil.which("cc")
        self.assertTrue(self.make and self.conf and self.compiler)

    def invoke(self, *goals):
        return subprocess.run(
            [self.make, "--no-print-directory", "-C", str(self.source),
             f"CDK2_BUILD_DIR={self.build}", f"CDK2_KCONFIG_TOOL={self.conf}",
             f"CDK2_NATIVE_HOST_CC={self.compiler}", *goals],
            env=self.environment, capture_output=True, text=True,
        )

    def test_public_config_then_ready_builds_actual_pinned_scanner(self):
        configured = self.invoke("CDK2_CONFIG_READY=0", "config")
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        self.assertTrue((self.build / "include/cdk2/config.h").is_file())
        compiled = self.invoke("CDK2_CONFIG_READY=1", str(self.scanner))
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        self.assertTrue(self.scanner.is_file())
        self.assertTrue(os.access(self.scanner, os.X_OK))

    def test_absolute_goal_without_ready_is_not_a_public_frontdoor(self):
        result = self.invoke("CDK2_CONFIG_READY=0", str(self.scanner))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No rule to make target", result.stderr)
        self.assertFalse(self.scanner.exists())

    def test_public_config_refuses_invalid_existing_config(self):
        configured = self.invoke("CDK2_CONFIG_READY=0", "config")
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        config = self.build / ".config"
        config.write_text(config.read_text() + "\nCONFIG_CDK2_BOOT_TIMEOUT=0\n")
        refused = self.invoke("CDK2_CONFIG_READY=0", "config")
        self.assertNotEqual(refused.returncode, 0)
        self.assertIn("duplicate CONFIG_CDK2_BOOT_TIMEOUT", refused.stderr)
        self.assertFalse(self.scanner.exists())


class FinalRomAdmission(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cdk2-final-rom-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = self.directory / "cdk2"
        (self.source / "util").mkdir(parents=True)
        self.calls = self.directory / "calls"
        self.environment = os.environ.copy()
        for name in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES"):
            self.environment.pop(name, None)
        self.environment["CALLS"] = str(self.calls)
        self.environment["SCANNER_STATUS"] = "0"
        self.environment["CONFIG_STATUS"] = "0"
        self.environment["ADMISSION_STATUS"] = "0"
        self.make = shutil.which("make")
        self.assertIsNotNone(self.make)
        self.fake_make = self.directory / "nested-make"
        self.fake_make.write_text(
            '#!/bin/sh\nset -eu\n'
            'for argument do\n'
            '  if [ "$argument" = config ]; then\n'
            '    printf "config\\n" >> "$CALLS"\n'
            '    printf "%s\\n" "$@" > "$CALLS.config-args"\n'
            '    exit "$CONFIG_STATUS"\n'
            '  fi\n'
            'done\n'
            'printf "scanner\\n" >> "$CALLS"\n'
            'printf "%s\\n" "$@" > "$CALLS.scanner-args"\n'
            'exit "$SCANNER_STATUS"\n'
        )
        self.fake_make.chmod(0o755)
        (self.source / "util/strict-direct-cbfs-admission").write_text(
            '#!/bin/sh\nset -eu\n'
            'test "$(cat "$3")" = final-rom\n'
            'printf "admission\\n" >> "$CALLS"\n'
            'printf "%s\\n" "$@" > "$CALLS.admission-args"\n'
            'exit "$ADMISSION_STATUS"\n'
        )

    def invoke(self, selected="y", source=None):
        include = ROOT / "payloads/external/Makefile.mk"
        if source is not None:
            include = self.directory / "external.mk"
            include.write_text(source)
        model = self.directory / "model.mk"
        model.write_text(
            '.DEFAULT_GOAL := finalised_rom\n'
            'strip_quotes = $(subst ",,$(1))\n'
            f'include {include}\n'
            '.PHONY: coreboot build_complete files_added finalised_rom\n'
            'coreboot:\n'
            '\t@printf "initial-rom\\n" > "$(obj)/coreboot.rom"\n'
            '\t@printf "coreboot\\n" >> "$(CALLS)"\n'
            'build_complete:: | coreboot\n'
            '\t@printf "build-complete\\n" >> "$(CALLS)"\n'
            'files_added:: | build_complete\n'
            '\t@printf "final-rom\\n" > "$(obj)/coreboot.rom"\n'
            '\t@printf "late-files\\n" >> "$(CALLS)"\n'
            'finalised_rom:: | files_added\n'
        )
        return subprocess.run(
            [self.make, "--no-print-directory", "-j8", "-f", str(model),
             f"CONFIG_PAYLOAD_CDK2={selected}",
             f"CDK2_SOURCE={self.source}", f"MAKE={self.fake_make}",
             f"CONFIG_PAYLOAD_FILE=\"{self.directory}/cdk2-build/native/payload.elf\"",
             'CONFIG_CBFS_PREFIX="fallback"', f"obj={self.directory}",
             f"objutil={self.directory}/tools", f"DOTCONFIG={self.directory}/outer.config",
             f"CBFSTOOL={self.directory}/tools/cbfstool", "HOSTCC=bound-host-cc"],
            cwd=self.directory, env=self.environment, capture_output=True, text=True,
        )

    def test_after_late_files_under_parallel_make(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.calls.read_text().splitlines(),
                         ["coreboot", "build-complete", "late-files", "config", "scanner", "admission"])

    def test_exact_admission_arguments(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(Path(str(self.calls) + ".admission-args").read_text().splitlines(),
                         [str(self.directory / "outer.config"),
                          str(self.directory / "cdk2-build/native/payload.elf"),
                          str(self.directory / "coreboot.rom"),
                          str(self.directory / "tools/cbfstool"), "fallback",
                          str(self.directory / "cdk2-build/strict-direct-cbfs-admission.tsv"),
                          str(self.directory / "cdk2-build/native/strict-direct-cbfs-raw-envelope")])

    def test_scanner_uses_existing_build_and_host_compiler(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(Path(str(self.calls) + ".scanner-args").read_text().splitlines(),
                         ["-C", str(self.source),
                          f"CDK2_BUILD_DIR={self.directory}/cdk2-build",
                          f"CDK2_CONFIG={self.directory}/cdk2-build/.config",
                          f"CDK2_DEFCONFIG={self.directory}/cdk2-build/coreboot.defconfig",
                          f"COREBOOT_CONFIG={self.directory}/outer.config",
                          f"CDK2_KCONFIG_TOOL={self.directory}/tools/kconfig/conf",
                          "CDK2_NATIVE_HOST_CC=bound-host-cc",
                          "CDK2_CONFIG_READY=1",
                          str(self.directory / "cdk2-build/native/strict-direct-cbfs-raw-envelope")])

    def test_config_failure_prevents_ready_and_admission(self):
        self.environment["CONFIG_STATUS"] = "5"
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls.read_text().splitlines(),
                         ["coreboot", "build-complete", "late-files", "config"])
        arguments = Path(str(self.calls) + ".config-args").read_text().splitlines()
        self.assertEqual(arguments[-2:], ["CDK2_CONFIG_READY=0", "config"])

    def test_scanner_failure_prevents_admission(self):
        self.environment["SCANNER_STATUS"] = "7"
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("admission", self.calls.read_text().splitlines())

    def test_validator_failure_propagates(self):
        self.environment["ADMISSION_STATUS"] = "9"
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("admission", self.calls.read_text().splitlines())

    def test_other_payload_has_no_cdk2_hook(self):
        result = self.invoke(selected="n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.calls.read_text().splitlines(),
                         ["coreboot", "build-complete", "late-files"])

    def test_missing_order_dependency_is_caught(self):
        source = (ROOT / "payloads/external/Makefile.mk").read_text()
        needle = "finalised_rom:: | files_added\n"
        self.assertEqual(source.count(needle), 1)
        result = self.invoke(source=source.replace(needle, "finalised_rom::\n", 1))
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
