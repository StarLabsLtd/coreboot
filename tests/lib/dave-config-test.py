#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Resolve DAVE Kconfig and compile the production MTL flash descriptions."""

import pathlib
import re
import shlex
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MTL = ROOT / "src/mainboard/starlabs/starfighter/variants/mtl"
FMAP_HEADER = struct.Struct("<8sBBQI32sH")
FMAP_AREA = struct.Struct("<II32sH")
PRESERVE = 1 << 3
ERASE_SIZE = 0x10000


class DaveConfigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="dave-config-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = pathlib.Path(cls.temporary.name)
        cls.fmaptool = cls.directory / "tools/cbfstool/fmaptool"
        cls.fmaptool.parent.mkdir(parents=True)
        cls.run_command([
            "make", "--no-print-directory", "-f", str(ROOT / "util/cbfstool/Makefile.mk"),
            f"top={ROOT}", f"objutil={cls.directory / 'tools'}", "HOSTCC=cc",
            f"VBOOT_SOURCE={ROOT / '3rdparty/vboot'}", "CONFIG_FMD_GENPARSER=n",
            str(cls.fmaptool),
        ])

    @staticmethod
    def run_command(command):
        result = subprocess.run(command, cwd=ROOT, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            raise AssertionError(f"{command!r}\n{result.stdout}")
        return result.stdout

    def resolve_config(self, overrides):
        settings = {
            "VENDOR_STARLABS": "y", "BOARD_STARLABS_STARFIGHTER_MTL": "y",
            "PAYLOAD_EDK2": "y", "VBOOT": "y", "TPM2": "y",
            "TPM_MEASURED_BOOT": "y", "TPM_HASH_SHA256": "y", "TPM_LOG_TPM2": "y",
            "SMMSTORE": "y", "ASS": "y", "EDK2_REPO_CUSTOM": "y",
            "DAVE_MODE": "y", **overrides,
        }
        config = self.directory / "config"
        config.write_text("".join(f"CONFIG_{key}={value}\n" for key, value in settings.items()))
        self.run_command([
            "make", "--no-print-directory", f"obj={self.directory / 'build'}",
            f"DOTCONFIG={config}", "olddefconfig",
        ])
        return dict(line.split("=", 1) for line in config.read_text().splitlines()
                    if line.startswith("CONFIG_") and "=" in line)

    def payload_flags(self, overrides):
        settings = {
            "CONFIG_DAVE_MODE": "y", "CONFIG_VBOOT": "y",
            "CONFIG_TPM_PREBOOT_AUTH_PHASE": "y", "CONFIG_SMMSTORE_SRK_PIN": "y",
            "CONFIG_BOOTMEDIA_SMM_BWP": "y", "CONFIG_EDK2_TAG_OR_REV": "1" * 40,
            "CONFIG_EDK2_REPOSITORY": "https://github.com/StarLabsLtd/dave",
            "CONFIG_SMMSTORE": "y", "CONFIG_ASS": "y", **overrides,
        }
        makefile = self.directory / "payload-test.mk"
        makefile.write_text(
            f"include {ROOT / 'payloads/external/edk2/Makefile'}\n"
            "dave-test-flags:\n\t@printf '%s\\n' '$(BUILD_STR)' '$(DAVE_FIXED_MM_ARGS)'\n")
        return subprocess.run(
            ["make", "--no-print-directory", "-f", str(makefile), "dave-test-flags",
             *(f"{key}={value}" for key, value in settings.items())],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def layout(self, vboot, pin, ab=False):
        label = f"{int(vboot)}-{int(pin)}-{int(ab)}"
        source = MTL / ("vboot.fmd" if vboot else "board.fmd")
        fmd = self.directory / f"layout-{label}.fmd"
        binary = self.directory / f"layout-{label}.fmap"
        fmd.write_text(self.run_command([
            "cc", "-E", "-P", "-x", "c", f"-DCONFIG_SMMSTORE_SRK_PIN={int(pin)}",
            f"-DCONFIG_VBOOT_SLOTS_RW_AB={int(ab)}", str(source),
        ]))
        self.run_command([str(self.fmaptool), str(fmd), str(binary)])
        data = binary.read_bytes()
        signature, major, minor, base, size, name, count = FMAP_HEADER.unpack_from(data)
        self.assertEqual((signature, major, minor, size), (b"__FMAP__", 1, 1, 0x2000000))
        self.assertEqual(len(data), FMAP_HEADER.size + count * FMAP_AREA.size)
        areas = {name.rstrip(b"\0").decode("ascii"): (0, size, 0)}
        for index in range(count):
            offset, size, name, flags = FMAP_AREA.unpack_from(
                data, FMAP_HEADER.size + index * FMAP_AREA.size)
            name = name.rstrip(b"\0").decode("ascii")
            self.assertNotIn(name, areas)
            areas[name] = (offset, size, flags)
        return areas

    def test_dave_requires_supported_verified_pin_backend(self):
        cases = (
            ({}, True),
            ({"SMMSTORE_SRK_PROVISION": "y"}, True),
            ({"VBOOT_SLOTS_RW_AB": "y"}, True),
            ({"DRIVERS_EFI_FW_INFO": "y", "DRIVERS_EFI_UPDATE_CAPSULES": "y",
              "DRIVERS_EFI_CAPSULE_ACCEPT_EMBEDDED_DRIVERS": "y"}, True),
            ({"VBOOT": "n"}, False),
            ({"VBOOT_MOCK_SECDATA": "y"}, False),
            ({"SMMSTORE": "n"}, False),
            ({"ASS": "n"}, False),
            ({"BOARD_STARLABS_STARFIGHTER_MTL": "n", "BOARD_STARLABS_ADL_HORIZON": "y",
              "SMMSTORE_SRK_PIN": "y"}, False),
        )
        for overrides, expected in cases:
            with self.subTest(overrides=overrides):
                values = self.resolve_config(overrides)
                self.assertEqual(values.get("CONFIG_DAVE_MODE") == "y", expected)
                if expected:
                    for option in ("VBOOT", "HAVE_SMMSTORE_SRK_PIN", "SMMSTORE_SRK_PIN", "ASS",
                                   "TPM_PREBOOT_AUTH_PHASE", "BOOTMEDIA_SMM_BWP",
                                   "SECURITY_FAIL_CLOSED"):
                        self.assertEqual(values.get(f"CONFIG_{option}"), "y", option)
                    self.assertNotEqual(values.get("CONFIG_VBOOT_MOCK_SECDATA"), "y")
                    self.assertNotEqual(values.get("CONFIG_BOOTMEDIA_SMM_BWP_RUNTIME_OPTION"), "y")
                    self.assertNotEqual(
                        values.get("CONFIG_DRIVERS_EFI_CAPSULE_ACCEPT_EMBEDDED_DRIVERS"), "y")
                    self.assertEqual(values.get("CONFIG_EDK2_REPO_CUSTOM"), "y")
                    self.assertEqual(values.get("CONFIG_EDK2_REPOSITORY"),
                                     '"https://github.com/StarLabsLtd/dave"')
                if overrides.get("BOARD_STARLABS_ADL_HORIZON") == "y":
                    self.assertNotEqual(values.get("CONFIG_SMMSTORE_SRK_PIN"), "y")

    def test_pin_and_fmap_occupy_dedicated_erase_blocks(self):
        for vboot, ab in ((False, False), (True, False), (True, True)):
            with self.subTest(vboot=vboot, ab=ab):
                areas = self.layout(vboot, True, ab)
                self.assertNotIn("CONSOLE", areas)
                self.assertEqual(areas["SRK_PIN_V1"][:2], (0x10c0000, ERASE_SIZE))
                self.assertEqual(areas["FMAP"][1], ERASE_SIZE)
                for name in ("SRK_PIN_V1", "FMAP"):
                    offset, size, flags = areas[name]
                    self.assertEqual(offset % ERASE_SIZE, 0)
                    self.assertTrue(flags & PRESERVE)
                    for other, (start, length, _) in areas.items():
                        if other == name or other in ("FLASH", "SI_BIOS", "WP_RO"):
                            continue
                        self.assertFalse(offset < start + length and start < offset + size,
                                         f"{name} shares an erase block with {other}")

    def test_signed_slots_and_non_pin_layout_remain_unchanged(self):
        for ab in (False, True):
            with self.subTest(ab=ab):
                old = self.layout(True, False, ab)
                new = self.layout(True, True, ab)
                self.assertNotIn("SRK_PIN_V1", old)
                self.assertEqual(old["FMAP"][1], 0x1000)
                for name in ("WP_RO", "RW_NVRAM", "EC", "SMMSTORE", "RW_MRC_CACHE",
                             "RW_SECTION_A", "FW_MAIN_A", "VBLOCK_A", "RW_FWID_A"):
                    self.assertEqual(old[name], new[name], name)
                if ab:
                    for name in ("RW_SECTION_B", "FW_MAIN_B", "VBLOCK_B", "RW_FWID_B"):
                        self.assertEqual(old[name], new[name], name)
                self.assertEqual(old["COREBOOT"][0], old["WP_RO"][0] + 0x5000)
                self.assertEqual(new["COREBOOT"][0], new["WP_RO"][0] + 0x14000)
                self.assertEqual(new["COREBOOT"][0] + new["COREBOOT"][1], 0x2000000)

    def test_capsule_fixture_matches_production_layout(self):
        areas = self.layout(True, True)
        header = (ROOT / "tests/drivers/dave-fmap-include/fmap_config.h").read_text()
        for name, field, value in re.findall(
                r"^#define FMAP_SECTION_(\w+)_(START|SIZE) (\w+)$", header, re.MULTILINE):
            self.assertEqual(int(value, 0), areas[name][field == "SIZE"], f"{name}_{field}")

    def test_payload_and_mm_owner_receive_dave_contract(self):
        for provisioning in ("n", "y"):
            with self.subTest(provisioning=provisioning):
                result = self.payload_flags({"CONFIG_SMMSTORE_SRK_PROVISION": provisioning})
                self.assertEqual(result.returncode, 0, result.stdout)
                payload, owner = (shlex.split(line) for line in result.stdout.splitlines())
                for setting in ("STARLABS_DAVE=TRUE", "PRE_OS_AUTH_PHASE_ENABLE=TRUE",
                                "PRE_OS_AUTH_PHASE_PCR=6", "SECURE_KEY_FACTORY_ENABLE=TRUE",
                                "STATIC_SECURE_BOOT=TRUE", "SECURE_BOOT_ENABLE=TRUE",
                                "OPAL_PASSWORD_ENABLE=TRUE", "COREBOOT_DAVE_VBOOT=TRUE",
                                "COREBOOT_DAVE_SRK_PIN=TRUE"):
                    self.assertIn(setting, payload)
                self.assertIn("SECURE_KEY_FACTORY_PROVISION_ENABLE=" +
                              ("TRUE" if provisioning == "y" else "FALSE"), payload)
                for setting in ("STARLABS_DAVE=TRUE", "STATIC_SECURE_BOOT=TRUE",
                                "COREBOOT_DAVE_VBOOT=TRUE", "COREBOOT_DAVE_SRK_PIN=TRUE",
                                "gUefiPayloadPkgTokenSpaceGuid.PcdStaticSecureBoot=TRUE"):
                    self.assertIn(setting, owner)

    def test_payload_rejects_conflicting_security_parameters(self):
        forbidden = (
            "-D STARLABS_DAVE=FALSE", "-DPRE_OS_AUTH_PHASE_ENABLE=FALSE",
            "--define=PRE_OS_AUTH_PHASE_PCR=7", "-D SECURE_KEY_FACTORY_ENABLE=FALSE",
            "-D SECURE_KEY_FACTORY_PROVISION_ENABLE=TRUE", "-D STATIC_SECURE_BOOT=FALSE",
            "-D OPAL_PASSWORD_ENABLE=FALSE", "-D COREBOOT_DAVE_VBOOT=FALSE",
            "-D COREBOOT_DAVE_SRK_PIN=FALSE",
            "-D ASS=FALSE", "-D VARIABLE_SUPPORT=SMMSTORE",
            "--pcd gUefiPayloadPkgTokenSpaceGuid.PcdDaveSecureBootPolicy=FALSE",
            "--pcd=gUefiPayloadPkgTokenSpaceGuid.PcdSmmStoreSrkPinRequired=FALSE",
            "--pcd gUefiPayloadPkgTokenSpaceGuid.PcdStaticSecureBoot=FALSE",
        )
        for variable in ("CONFIG_EDK2_CUSTOM_BUILD_PARAMS", "EDK2_EXTRA_BUILD_PARAMS", "BUILD_STR"):
            for parameter in forbidden:
                with self.subTest(variable=variable, parameter=parameter):
                    result = self.payload_flags({variable: parameter})
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn("must preserve the verified-boot authentication contract", result.stdout)
        for option in ("CONFIG_VBOOT", "CONFIG_TPM_PREBOOT_AUTH_PHASE", "CONFIG_ASS",
                       "CONFIG_SMMSTORE_SRK_PIN", "CONFIG_BOOTMEDIA_SMM_BWP"):
            with self.subTest(option=option):
                self.assertNotEqual(self.payload_flags({option: "n"}).returncode, 0)
        for revision in ("", "origin/starlabs", "1" * 39):
            with self.subTest(revision=revision):
                self.assertNotEqual(self.payload_flags({"CONFIG_EDK2_TAG_OR_REV": revision}).returncode, 0)

    def test_dave_retains_verified_boot_release_parameters(self):
        result = self.payload_flags({
            "CONFIG_STARLABS_VBOOT_RELEASE_POLICY": "y", "CONFIG_EDK2_SECURE_BOOT_SUPPORT": "y",
            "CONFIG_DRIVERS_EFI_CAPSULE_REGIONS": "EC RW_NVRAM FW_MAIN_A RW_FWID_A VBLOCK_A",
            "CONFIG_DRIVERS_EFI_MAIN_FW_GUID": "eef5c7c3-124f-406a-87fe-4b58589fd331",
        })
        self.assertEqual(result.returncode, 0, result.stdout)
        payload = shlex.split(result.stdout.splitlines()[0])
        for setting in (
                "gUefiPayloadPkgTokenSpaceGuid.PcdDefaultSecureBootEnable=TRUE",
                "gEfiMdeModulePkgTokenSpaceGuid.PcdTpm2MeasurementRequired=TRUE",
                "gEfiSecurityPkgTokenSpaceGuid.PcdOptionRomImageVerificationPolicy=0x04",
                "gEfiSecurityPkgTokenSpaceGuid.PcdFixedMediaImageVerificationPolicy=0x04",
                "gEfiSecurityPkgTokenSpaceGuid.PcdRemovableMediaImageVerificationPolicy=0x04"):
            self.assertIn(setting, payload)

    def test_failed_payload_build_cannot_reuse_output(self):
        workspace = self.directory / "failed-build/workspace"
        source = workspace / "source"
        source.mkdir(parents=True)
        marker = workspace / "build-attempted"
        output = workspace / "Build/UefiPayloadPkgX64/RELEASE_GCC/FV/UEFIPAYLOAD.fd"
        output.parent.mkdir(parents=True)
        output.write_bytes(b"output from a failed build")
        setup = source / "edksetup.sh"
        setup.write_text(f"build() {{ touch {shlex.quote(str(marker))}; return 23; }}\n")
        makefile = self.directory / "failed-build.mk"
        makefile.write_text(
            f"include {ROOT / 'payloads/external/edk2/Makefile'}\n"
            ".PHONY: dave-force-build\n"
            f"{output}: dave-force-build\n")
        command = ["make", "--no-print-directory", "-f", str(makefile),
                   "-o", "prep", "-o", "print", str(output),
                   f"WORKSPACE={workspace}", f"EDK2_PATH={source}",
                   "CONFIG_EDK2_REPOSITORY=https://github.com/StarLabsLtd/dave"]
        failed = subprocess.run(command, cwd=ROOT, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertTrue(marker.exists(), failed.stdout)
        self.assertNotEqual(failed.returncode, 0, failed.stdout)
        self.assertNotIn("Success!", failed.stdout)
        self.assertTrue(output.exists())

        setup.write_text("build() { return 0; }\n")
        self.run_command(command)

    def test_source_must_declare_payload_and_owner_contract(self):
        source = self.directory / "source/UefiPayloadPkg"
        source.mkdir(parents=True)
        payload_options = ("STARLABS_DAVE", "COREBOOT_DAVE_VBOOT", "COREBOOT_DAVE_SRK_PIN")
        owner_options = (*payload_options, "STATIC_SECURE_BOOT")
        payload_dsc = source / "UefiPayloadPkg.dsc"
        owner_dsc = source / "CorebootFixedMmService.dsc"
        check = ROOT / "payloads/external/edk2/check-dave-source.sh"
        self.run_command(["git", "-C", str(source.parent), "init", "-q"])
        for missing in (*payload_options, "owner", None):
            with self.subTest(missing=missing):
                payload_dsc.write_text("".join(
                    f"  DEFINE {option} = FALSE\n" for option in payload_options if option != missing))
                owner_dsc.write_text("".join(
                    f"  DEFINE {option} = FALSE\n" for option in owner_options if missing != "owner"))
                self.run_command(["git", "-C", str(source.parent), "add", "UefiPayloadPkg"])
                result = subprocess.run([str(check), str(source.parent), "y"], text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                self.assertEqual(result.returncode == 0, missing is None, result.stdout)


if __name__ == "__main__":
    unittest.main()
