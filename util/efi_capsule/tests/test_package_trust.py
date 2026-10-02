#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from package_trust import package
from crypto_policy import CryptoPolicyError, MAX_CERTIFICATE_SIZE, MAX_CERTIFICATE_BYTES


class TrustPackageTest(unittest.TestCase):
    def test_runtime_capacity_parity(self):
        source = pathlib.Path(__file__).resolve().parents[3]
        header = (source / "src/include/payload_mm_cms.h").read_text()
        for name, expected in (
            ("PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE", MAX_CERTIFICATE_SIZE),
            ("PAYLOAD_MM_MAX_TRUST_XDR_SIZE", MAX_CERTIFICATE_BYTES),
        ):
            match = re.search(r"^#define " + name + r" \(([0-9]+)U \* 1024U\)$",
                              header, re.MULTILINE)
            self.assertIsNotNone(match)
            self.assertEqual(int(match.group(1)) * 1024, expected)

    def test_public_certificate_and_refused_inputs(self):
        with tempfile.TemporaryDirectory(prefix="capsule-trust-test-") as temporary:
            root = pathlib.Path(temporary)
            key, pem, output = [root / name for name in ("key.pem", "cert.pem", "trust.der")]
            subprocess.run(
                ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                 "-subj", "/CN=package fixture/", "-days", "1", "-keyout", str(key),
                 "-out", str(pem)], check=True, capture_output=True,
            )
            certificate = pem.read_bytes()
            alias = root / "alias.pem"
            hardlink = root / "hardlink.pem"
            alias.symlink_to(pem)
            hardlink.hardlink_to(pem)
            script = pathlib.Path(__file__).resolve().parents[1] / "package_trust.py"
            for destination in (pem, alias, hardlink):
                with self.subTest(alias=destination.name):
                    with self.assertRaises(CryptoPolicyError):
                        package(pem, destination)
                    result = subprocess.run(
                        [sys.executable, str(script), "--certificate", str(pem),
                         "--output", str(destination)], capture_output=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(pem.read_bytes(), certificate)
                    self.assertEqual(destination.read_bytes(), certificate)
            package(pem, output)
            expected = subprocess.check_output(
                ["openssl", "x509", "-in", str(pem), "-outform", "DER"])
            self.assertEqual(output.read_bytes(), expected)
            for rejected in (b"", b"not PEM", key.read_bytes(),
                             certificate + key.read_bytes(), certificate * 2,
                             b"-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n"):
                with self.subTest(input_size=len(rejected)):
                    pem.write_bytes(rejected)
                    output.write_bytes(b"stale trust")
                    with self.assertRaises((CryptoPolicyError, subprocess.CalledProcessError)):
                        package(pem, output)
                    self.assertFalse(output.exists())
            output.write_bytes(b"stale trust")
            with self.assertRaises(OSError):
                package(root / "missing.pem", output)
            self.assertFalse(output.exists())
            self.assertEqual(list(root.glob(".trust-*")), [])
            # Exercise the real selected Make recipe, including missing input
            # after an earlier successful package rather than only the CLI.
            pem.write_bytes(certificate)
            config = root / "config"
            config.touch()
            makefile = (
                'strip_quotes = $(subst ",,$(1))\n'
                "include src/drivers/efi/Makefile.mk\n"
                "all: $(obj)/capsule/trust.der\n"
            )
            command = ["make", "-f", "-", "all", f"obj={root}",
                       f"DOTCONFIG={config}", "CONFIG_CAPSULE_BROKER_TRUST_PACKAGE=y",
                       f"CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT={pem}"]
            source = pathlib.Path(__file__).resolve().parents[3]
            result = subprocess.run(command, input=makefile, text=True, cwd=source,
                                    capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            built = root / "capsule/trust.der"
            self.assertEqual(built.read_bytes(), expected)
            pem.unlink()
            result = subprocess.run(command, input=makefile, text=True, cwd=source,
                                    capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(built.exists())


if __name__ == "__main__":
    unittest.main()
