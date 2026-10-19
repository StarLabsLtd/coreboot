#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import shutil
from pathlib import Path
import subprocess
import tempfile
import unittest


EDK2_DIR = Path(__file__).parents[1]
TOP = EDK2_DIR.parents[2]
CBFSTOOL = TOP / "util/cbfstool/cbfstool"
FMAPTOOL = TOP / "util/cbfstool/fmaptool"


def run(*args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, text=True, **kwargs)


class EDK2MakeIntegrationTest(unittest.TestCase):
    def test_release_checkout_uses_payload_checker(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            payload_dir = directory / "edk2"
            shutil.copytree(EDK2_DIR, payload_dir)
            source = payload_dir / "workspace/edk2"
            source.mkdir(parents=True)
            run("git", "init", str(source))
            run("git", "-C", str(source), "config", "user.email", "test@example.com")
            run("git", "-C", str(source), "config", "user.name", "Test")
            (source / "file").write_text("test\n")
            run("git", "-C", str(source), "add", "file")
            run("git", "-C", str(source), "commit", "-m", "test")
            revision = run("git", "-C", str(source), "rev-parse", "HEAD").stdout.strip()
            run("make", "-C", str(payload_dir), str(source),
                "CONFIG_STARLABS_VBOOT_RELEASE_POLICY=y", "CONFIG_VBOOT=y",
                "CONFIG_EDK2_REPOSITORY=https://example.invalid/edk2",
                f"CONFIG_EDK2_TAG_OR_REV={revision}",
                "CONFIG_DRIVERS_EFI_CAPSULE_REGIONS=EC RW_NVRAM FW_MAIN_A RW_FWID_A VBLOCK_A",
                "CONFIG_LOCALVERSION=26.09")

    def test_release_capsule_extracts_prefixed_rw_payload(self):
        result = run(
            "make", "-C", str(EDK2_DIR), "-n", "coreboot_capsule",
            "COREBOOT_ROM=/tmp/coreboot.rom", "COREBOOT_CAPSULE_OUT=/tmp/coreboot.cap",
            "COREBOOT_FMAP_LAYOUT=/tmp/fmap.fmap", "CBFSTOOL=/tmp/cbfstool",
            "CONFIG_STARLABS_VBOOT_RELEASE_POLICY=y", "CONFIG_VBOOT=y",
            "CONFIG_CBFS_PREFIX=normal", "CONFIG_EDK2_REPOSITORY=https://example.invalid/edk2",
            "CONFIG_EDK2_TAG_OR_REV=0123456789012345678901234567890123456789",
            "CONFIG_DRIVERS_EFI_MAIN_FW_GUID=b96a6b09-dc2c-4e3a-86ee-47016c7b035a",
            "CONFIG_LOCALVERSION=26.09",
            "CONFIG_DRIVERS_EFI_CAPSULE_REGIONS=EC RW_NVRAM FW_MAIN_A RW_FWID_A VBLOCK_A",
            "CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=0", "CONFIG_DRIVERS_EFI_MAIN_FW_LSV=0",
            "CONFIG_DRIVERS_EFI_CAPSULE_EMBED_FMP_DXE=n",
            "CONFIG_DRIVERS_EFI_CAPSULE_INITIATE_RESET=n",
            "CONFIG_DRIVERS_EFI_CAPSULE_SIGNER_PRIVATE_CERT=key.pem",
            "CONFIG_DRIVERS_EFI_CAPSULE_OTHER_PUBLIC_CERT=other.pem",
            "CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT=trusted.pem")
        self.assertIn('extract -r FW_MAIN_A', result.stdout)
        self.assertIn('-n "normal/payload"', result.stdout)

    @unittest.skipUnless(CBFSTOOL.exists() and FMAPTOOL.exists(), "cbfstool is not built")
    def test_cbfs_extracts_only_the_prefixed_rw_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            fmd = directory / "map.fmd"
            fmap = directory / "map.fmap"
            rom = directory / "rom"
            rw_payload = directory / "rw"
            recovery_payload = directory / "recovery"
            extracted = directory / "extracted"
            fmd.write_text("FLASH 0x4000 {\n\tFMAP 0x1000\n"
                           "\tFW_MAIN_A(CBFS) 0x1000\n"
                           "\tCOREBOOT(CBFS) 0x2000\n}\n")
            rw_payload.write_bytes(b"rw payload")
            recovery_payload.write_bytes(b"recovery payload")
            run(str(FMAPTOOL), str(fmd), str(fmap))
            run(str(CBFSTOOL), str(rom), "create", "-M", str(fmap), "-r",
                "FW_MAIN_A,COREBOOT")
            run(str(CBFSTOOL), str(rom), "add", "-r", "FW_MAIN_A", "-n",
                "normal/payload", "-f", str(rw_payload), "-t", "raw", "-c", "lz4")
            run(str(CBFSTOOL), str(rom), "add", "-r", "COREBOOT", "-n",
                "normal/payload", "-f", str(recovery_payload), "-t", "raw")
            run(str(CBFSTOOL), str(rom), "extract", "-r", "FW_MAIN_A", "-n",
                "normal/payload", "-f", str(extracted))
            self.assertEqual(extracted.read_bytes(), rw_payload.read_bytes())
            missing = subprocess.run(
                (str(CBFSTOOL), str(rom), "extract", "-r", "FW_MAIN_A", "-n",
                 "payload", "-f", str(extracted)), capture_output=True, text=True)
            self.assertNotEqual(missing.returncode, 0)


if __name__ == "__main__":
    unittest.main()
