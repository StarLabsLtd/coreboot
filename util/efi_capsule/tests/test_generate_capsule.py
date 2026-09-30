#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import pathlib
import struct
import subprocess
import tempfile
import unittest
import uuid
from unittest.mock import patch

from util.efi_capsule.append_rmap import build_manifest
from util.efi_capsule.crypto_policy import CryptoPolicyError
from util.efi_capsule.generate_capsule import build_capsule
from util.efi_capsule.validate_capsule import (
    ValidationError,
    parse_capsule,
    parse_capsule_details,
    validate,
)


IMAGE_GUID = uuid.UUID("975cd0e6-c540-4e2b-906c-72c0d0d1e40d")


def fmap_image():
    image = bytearray(256)
    struct.pack_into("<8sBBQI32sH", image, 0, b"__FMAP__", 1, 1, 0, len(image),
                     b"test", 2)
    struct.pack_into("<II32sH", image, 56, 0, 140, b"FMAP", 0)
    struct.pack_into("<II32sH", image, 98, 0, len(image), b"COREBOOT", 0)
    return bytes(image)


def fmap_with_areas(areas, image_size=2048):
    image = bytearray(image_size)
    table_size = 56 + 42 * len(areas)
    struct.pack_into("<8sBBQI32sH", image, 0, b"__FMAP__", 1, 1, 0,
                     image_size, b"test", len(areas))
    for index, (start, length, name, flags) in enumerate(areas):
        struct.pack_into("<II32sH", image, 56 + 42 * index, start, length,
                         name.encode("ascii"), flags)
    if table_size > image_size:
        raise ValueError("FMAP table does not fit in test image")
    return bytes(image)


def signer_file(directory, key_arguments, name="signer"):
    key = pathlib.Path(directory, f"{name}-key.pem")
    certificate = pathlib.Path(directory, f"{name}-certificate.pem")
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", *key_arguments, "-nodes",
         "-subj", f"/CN={name}", "-days", "1", "-keyout", str(key),
         "-out", str(certificate)], check=True, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    signer = pathlib.Path(directory, f"{name}.pem")
    signer.write_bytes(key.read_bytes() + certificate.read_bytes())
    return signer, certificate


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
        details = parse_capsule_details(capsule)
        self.assertEqual(details["fw_version"], 0x001a0009)
        self.assertEqual(details["lsv"], 1)
        self.assertEqual(details["image"], b"firmware")

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

    @patch("util.efi_capsule.generate_capsule.sign", return_value=b"signature")
    def test_validates_exact_image_and_rmap(self, _signer):
        image = fmap_image() + build_manifest(["COREBOOT"])
        capsule = build_capsule(image, IMAGE_GUID, 9, 4, "signer", None, "root")
        validate(capsule, None, IMAGE_GUID, 0, 9, 4, image, ["COREBOOT"],
                 expect_rmap=True)
        with self.assertRaisesRegex(ValidationError, "payload image"):
            validate(capsule, None, IMAGE_GUID, 0, expected_image=image + b"x")
        with self.assertRaisesRegex(ValidationError, "RMAP region list"):
            validate(capsule, None, IMAGE_GUID, 0,
                     expected_regions=["RECOVERY"], expect_rmap=True)

    @patch("util.efi_capsule.generate_capsule.sign", return_value=b"signature")
    def test_validates_typed_broker_image_without_rmap(self, _signer):
        image = fmap_image()
        capsule = build_capsule(image, IMAGE_GUID, 9, 4, "signer", None, "root")
        validate(capsule, None, IMAGE_GUID, 0, 9, 4, image, ["COREBOOT"])

    def test_real_pkcs7_signature(self):
        with tempfile.TemporaryDirectory() as temporary:
            signer, certificate = signer_file(temporary, ["rsa:2048"])
            capsule = build_capsule(b"firmware", IMAGE_GUID, 2, 1,
                                    str(signer), None, str(certificate))
            validate(capsule, None, IMAGE_GUID, 0,
                     trusted_public_cert=str(certificate))
            changed = bytearray(capsule)
            changed[-1] ^= 1
            with self.assertRaisesRegex(ValidationError, "PKCS#7"):
                validate(bytes(changed), None, IMAGE_GUID, 0,
                         trusted_public_cert=str(certificate))

    def test_rejects_ec_and_short_rsa_signers(self):
        cases = (
            (["ec", "-pkeyopt", "ec_paramgen_curve:P-256"], "not RSA"),
            (["rsa:1024"], "unsupported RSA size 1024"),
        )
        for index, (key_arguments, message) in enumerate(cases):
            with (
                self.subTest(key_arguments=key_arguments),
                tempfile.TemporaryDirectory() as temporary,
            ):
                signer, certificate = signer_file(
                    temporary, key_arguments, f"unsupported-{index}")
                with self.assertRaisesRegex(CryptoPolicyError, message):
                    build_capsule(b"firmware", IMAGE_GUID, 2, 1,
                                  str(signer), None, str(certificate))

    def test_rejects_excess_cms_certificate_chain(self):
        with tempfile.TemporaryDirectory() as temporary:
            signer, certificate = signer_file(temporary, ["rsa:2048"])
            extra_key = pathlib.Path(temporary, "extra-key.pem")
            subprocess.run(
                ["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt",
                 "rsa_keygen_bits:2048", "-out", str(extra_key)], check=True,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            intermediates = pathlib.Path(temporary, "intermediates.pem")
            certificates = []
            for index in range(8):
                extra = pathlib.Path(temporary, f"extra-{index}.pem")
                subprocess.run(
                    ["openssl", "req", "-x509", "-new", "-key", str(extra_key),
                     "-subj", f"/CN=extra-{index}", "-set_serial", str(index + 2),
                     "-days", "1", "-out", str(extra)], check=True,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                certificates.append(extra.read_bytes())
            intermediates.write_bytes(b"".join(certificates))
            with self.assertRaisesRegex(CryptoPolicyError, "more than 8"):
                build_capsule(b"firmware", IMAGE_GUID, 2, 1, str(signer),
                              str(intermediates), str(certificate))

    @patch("util.efi_capsule.generate_capsule.sign", return_value=b"signature")
    def test_rejects_lowest_version_above_version(self, _signer):
        with self.assertRaisesRegex(ValueError, "lowest-supported"):
            build_capsule(b"firmware", IMAGE_GUID, 1, 2, "signer", None, "root")

    @patch("util.efi_capsule.generate_capsule.sign", return_value=b"signature")
    def test_validator_matches_typed_fmap_rules(self, _signer):
        image = bytearray(fmap_image())
        image[10] = 1
        capsule = build_capsule(bytes(image), IMAGE_GUID, 2, 1,
                                "signer", None, "root")
        with self.assertRaisesRegex(ValidationError, "0 valid FMAP"):
            validate(capsule, None, IMAGE_GUID, 0, expected_regions=["COREBOOT"])

    @patch("util.efi_capsule.generate_capsule.sign", return_value=b"signature")
    def test_rejects_multiple_valid_fmaps(self, _signer):
        image = bytearray(fmap_image() + b"\0" * 256)
        first = bytearray(fmap_image())
        struct.pack_into("<I", first, 18, len(image))
        image[:len(first)] = first
        second = bytearray(first)
        struct.pack_into("<II", second, 56, 256, 140)
        struct.pack_into("<II", second, 98, 0, len(image))
        image[256:] = second
        capsule = build_capsule(bytes(image), IMAGE_GUID, 2, 1,
                                "signer", None, "root")
        with self.assertRaisesRegex(ValidationError, "2 valid FMAP"):
            validate(capsule, None, IMAGE_GUID, 0, expected_regions=["COREBOOT"])

    @patch("util.efi_capsule.generate_capsule.sign", return_value=b"signature")
    def test_rejects_more_than_runtime_fmap_area_limit(self, _signer):
        areas = [(0, 1442, "FMAP", 0), (0, 2048, "COREBOOT", 0)]
        areas += [(1500 + index, 1, f"AREA{index}", 0) for index in range(31)]
        image = fmap_with_areas(areas)
        capsule = build_capsule(image, IMAGE_GUID, 2, 1,
                                "signer", None, "root")
        with self.assertRaisesRegex(ValidationError, "0 valid FMAP"):
            validate(capsule, None, IMAGE_GUID, 0, expected_regions=["COREBOOT"])

    @patch("util.efi_capsule.generate_capsule.sign", return_value=b"signature")
    def test_rejects_unknown_fmap_area_flags(self, _signer):
        image = fmap_with_areas(((0, 140, "FMAP", 0),
                                 (0, 2048, "COREBOOT", 0x10)))
        capsule = build_capsule(image, IMAGE_GUID, 2, 1,
                                "signer", None, "root")
        with self.assertRaisesRegex(ValidationError, "0 valid FMAP"):
            validate(capsule, None, IMAGE_GUID, 0, expected_regions=["COREBOOT"])

    @patch("util.efi_capsule.generate_capsule.sign", return_value=b"signature")
    def test_rejects_partially_overlapping_fmap_areas(self, _signer):
        image = fmap_with_areas(((0, 224, "FMAP", 0),
                                 (0, 2048, "COREBOOT", 0),
                                 (512, 512, "LEFT", 0),
                                 (768, 512, "RIGHT", 0)))
        capsule = build_capsule(image, IMAGE_GUID, 2, 1,
                                "signer", None, "root")
        with self.assertRaisesRegex(ValidationError, "0 valid FMAP"):
            validate(capsule, None, IMAGE_GUID, 0, expected_regions=["COREBOOT"])

    @patch("util.efi_capsule.generate_capsule.sign", return_value=b"signature")
    def test_accepts_contained_fmap_areas(self, _signer):
        image = fmap_with_areas(((0, 224, "FMAP", 0),
                                 (0, 2048, "COREBOOT", 0),
                                 (512, 512, "PARENT", 0),
                                 (640, 128, "CHILD", 0x0f)))
        capsule = build_capsule(image, IMAGE_GUID, 2, 1,
                                "signer", None, "root")
        validate(capsule, None, IMAGE_GUID, 0, expected_image=image,
                 expected_regions=["COREBOOT"])


if __name__ == "__main__":
    unittest.main()
