#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import struct
import unittest
import uuid
from unittest.mock import patch

from util.efi_capsule.append_rmap import build_manifest
from util.efi_capsule.generate_capsule import build_capsule
from util.efi_capsule.validate_capsule import parse_capsule


IMAGE_GUID = uuid.UUID("975cd0e6-c540-4e2b-906c-72c0d0d1e40d")


class GenerateCapsuleTest(unittest.TestCase):
    @patch("util.efi_capsule.generate_capsule.sign", return_value=b"signature")
    def test_builds_valid_single_image_capsule(self, signer):
        capsule = build_capsule(b"firmware", IMAGE_GUID, 0x001a0009, 1,
                                "signer", "chain", "root")
        self.assertEqual(parse_capsule(capsule), (IMAGE_GUID, 0))
        signer.assert_called_once()
        signed = signer.call_args.args[0]
        self.assertEqual(signed[:4], b"MSS1")
        self.assertEqual(struct.unpack_from("<II", signed, 8), (0x001a0009, 1))
        self.assertEqual(signed[-8:], b"\0" * 8)

    @patch("util.efi_capsule.generate_capsule.sign", return_value=b"signature")
    def test_initiate_reset_and_embedded_driver(self, _signer):
        capsule = build_capsule(b"firmware", IMAGE_GUID, 2, 1, "signer",
                                None, "root", [b"MZ"], True)
        self.assertEqual(parse_capsule(capsule), (IMAGE_GUID, 1))
        self.assertEqual(struct.unpack_from("<I", capsule, 20)[0], 0x50000)

    def test_rmap_manifest(self):
        manifest = build_manifest(["COREBOOT", "EC"])
        self.assertEqual(manifest[:16], b"COREBOOT" + b"\0" * 8)
        self.assertEqual(manifest[16:32], b"EC" + b"\0" * 14)
        self.assertEqual(struct.unpack_from("<4sHH", manifest, 32),
                         (b"RMAP", 1, 2))

    def test_rmap_rejects_duplicates(self):
        with self.assertRaisesRegex(ValueError, "duplicate"):
            build_manifest(["COREBOOT", "COREBOOT"])


if __name__ == "__main__":
    unittest.main()
