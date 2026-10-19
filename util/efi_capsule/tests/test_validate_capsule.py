#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import struct
import tempfile
from pathlib import Path
import unittest
import uuid

from util.efi_capsule.validate_capsule import (
    FMP_CAPSULE_GUID,
    RMAP_SIGNATURE,
    ValidationError,
    validate_rmap,
    validate,
)


FMP_GUID = uuid.UUID("85584837-7b03-4b6c-940c-8b6186cbf7a1")
OTHER_GUID = uuid.UUID("83335bd5-ec9e-4a78-8931-223d990f5b44")


def make_authenticated_update(image, malformed=False):
    certificate = struct.pack(
        "<IHH16s", 24, 0x0200, 0x0EF1,
        uuid.UUID("4aafd29d-68df-49ee-8aa9-347d375665a7").bytes_le,
    )
    if malformed:
        certificate = b"\0" * len(certificate)
    return struct.pack("<Q", 0) + certificate + image


def make_fmp_payload(image):
    return struct.pack("<4sIII", b"MSS1", 16, 1, 1) + image


def make_capsule(image_guid=FMP_GUID, embedded_count=0, item_offset=None,
                 update_image=b"\0", authenticated=False, malformed_auth=False):
    embedded_items = [b"MZ" for _ in range(embedded_count)]
    update_image = make_fmp_payload(update_image)
    if authenticated:
        update_image = make_authenticated_update(update_image, malformed_auth)
    image = struct.pack(
        "<I16sB3xIIQQ",
        3,
        image_guid.bytes_le,
        1,
        len(update_image),
        0,
        0,
        int(authenticated),
    ) + update_image
    items = embedded_items + [image]

    table_size = 8 + len(items) * 8
    offsets = []
    offset = table_size
    for item in items:
        offsets.append(offset)
        offset += len(item)
    if item_offset is not None:
        offsets[-1] = item_offset

    fmp = struct.pack("<IHH", 1, embedded_count, 1)
    fmp += struct.pack(f"<{len(items)}Q", *offsets)
    fmp += b"".join(items)

    header_size = 32
    image_size = header_size + len(fmp)
    capsule = FMP_CAPSULE_GUID.bytes_le
    capsule += struct.pack("<III", header_size, 0x10000, image_size)
    capsule += b"\0" * (header_size - len(capsule))
    return capsule + fmp


def make_fv(file_guid=FMP_GUID, file_type=0x07, duplicate=False):
    fv_size = 0x1000
    header_size = 72
    fv = bytearray(b"\xff" * fv_size)
    fv[0:16] = b"\0" * 16
    fv[16:32] = uuid.UUID("8c8ce578-8a3d-4f1c-9935-896185c32dd3").bytes_le
    struct.pack_into(
        "<Q4sIHHHBB", fv, 32, fv_size, b"_FVH", 0x800, header_size, 0, 0, 0, 2
    )
    struct.pack_into("<II", fv, 56, 1, fv_size)
    struct.pack_into("<II", fv, 64, 0, 0)

    def add_file(offset):
        file_size = 32
        header = file_guid.bytes_le + b"\0\0"
        header += bytes([file_type, 0])
        header += file_size.to_bytes(3, "little") + b"\x07"
        fv[offset : offset + 24] = header
        fv[offset + 24 : offset + file_size] = b"\xa5" * 8
        return offset + file_size

    next_offset = add_file(header_size)
    if duplicate:
        add_file(next_offset)
    return bytes(fv)


def make_rmap_image(regions, overlap=False, nested=False, out_of_bounds=False):
    image = bytearray(b"\xff" * 0x1000)
    fmap_offset = 0x100
    struct.pack_into("<8sBBQI32sH", image, fmap_offset, b"__FMAP__", 1, 1,
                     0, len(image), b"test\0", len(regions) + nested)
    for index, name in enumerate(regions):
        offset = 0x400 + index * 0x100
        if overlap and index:
            offset -= 0x80
        if out_of_bounds and index == 0:
            offset = len(image) - 0x80
        struct.pack_into("<II32sH", image, fmap_offset + 56 + index * 42,
                         offset, 0x100, name.encode() + b"\0", 0)
    if nested:
        struct.pack_into("<II32sH", image, fmap_offset + 56 + len(regions) * 42,
                         0x300, 0x600, b"RW_SECTION_A\0", 0)
    entries = b"".join(name.encode().ljust(16, b"\0") for name in regions)
    return bytes(image) + entries + struct.pack("<IHH", RMAP_SIGNATURE, 1,
                                                  len(regions))


def rmap_firmware(image, region_count):
    return image[:-(region_count * 16 + 8)]


class ValidateCapsuleTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.module_dir = Path(directory.name)
        self.output = self.module_dir / str(FMP_GUID) / "OUTPUT"
        self.output.mkdir(parents=True)
        (self.output / "FmpDxe.inf").write_text(
            f"[Defines]\n  FILE_GUID = {FMP_GUID}\n[Binaries.X64]\n  PE32|FmpDxe.efi\n"
        )
        (self.output / "FmpDxe.efi").write_bytes(b"MZ")

    def test_matching_capsule_and_resident_driver(self):
        validate(make_capsule(), make_fv(), FMP_GUID, 0)

    def test_extended_ffs_header(self):
        fv = bytearray(make_fv())
        offset = 72
        file_size = 0x1000000
        fv.extend(b"\xff" * (offset + file_size - len(fv)))
        struct.pack_into("<Q", fv, 32, len(fv))
        struct.pack_into("<II", fv, 56, 1, len(fv))
        fv[offset + 19] = 0x01
        fv[offset + 20 : offset + 23] = b"\0" * 3
        struct.pack_into("<Q", fv, offset + 24, file_size)
        validate(make_capsule(), bytes(fv), FMP_GUID, 0)
        fv[offset + 20] = 1
        with self.assertRaisesRegex(ValidationError, "nonzero size field"):
            validate(make_capsule(), bytes(fv), FMP_GUID, 0)

    def test_matching_embedded_driver_count(self):
        validate(make_capsule(embedded_count=1), make_fv(), FMP_GUID, 1,
                 self.module_dir)

    def test_embedded_only_driver(self):
        validate(make_capsule(embedded_count=1), make_fv(OTHER_GUID), FMP_GUID, 1,
                 self.module_dir)

    def test_rejects_missing_embedded_metadata(self):
        with self.assertRaisesRegex(ValidationError, "metadata is required"):
            validate(make_capsule(embedded_count=1), make_fv(), FMP_GUID, 1)

    def test_rejects_embedded_identity_mismatch(self):
        (self.output / "FmpDxe.inf").write_text(
            f"[Defines]\n FILE_GUID = {OTHER_GUID}\n"
        )
        with self.assertRaisesRegex(ValidationError, "matching embedded FMP"):
            validate(make_capsule(embedded_count=1), make_fv(), FMP_GUID, 1,
                     self.module_dir)

    def test_rejects_stale_embedded_binary(self):
        (self.output / "FmpDxe.efi").write_bytes(b"old build")
        with self.assertRaisesRegex(ValidationError, "differs from"):
            validate(make_capsule(embedded_count=1), make_fv(), FMP_GUID, 1,
                     self.module_dir)

    def test_rejects_duplicate_build_descriptors(self):
        output = self.module_dir / "stale" / "OUTPUT"
        output.mkdir(parents=True)
        (output / "FmpDxe.inf").write_bytes((self.output / "FmpDxe.inf").read_bytes())
        with self.assertRaisesRegex(ValidationError, "one matching embedded FMP"):
            validate(make_capsule(embedded_count=1), make_fv(), FMP_GUID, 1,
                     self.module_dir)

    def test_rejects_capsule_image_guid_mismatch(self):
        with self.assertRaisesRegex(ValidationError, "does not match"):
            validate(make_capsule(OTHER_GUID), make_fv(), FMP_GUID, 0)

    def test_rejects_embedded_driver_count_mismatch(self):
        with self.assertRaisesRegex(ValidationError, "embedded drivers"):
            validate(make_capsule(embedded_count=1), make_fv(), FMP_GUID, 0)

    def test_rejects_missing_resident_driver(self):
        with self.assertRaisesRegex(ValidationError, "0 resident FMP drivers"):
            validate(make_capsule(), make_fv(OTHER_GUID), FMP_GUID, 0)

    def test_rejects_duplicate_resident_driver(self):
        with self.assertRaisesRegex(ValidationError, "2 resident FMP drivers"):
            validate(make_capsule(), make_fv(duplicate=True), FMP_GUID, 0)

    def test_rejects_matching_non_driver_ffs_file(self):
        with self.assertRaisesRegex(ValidationError, "0 resident FMP drivers"):
            validate(make_capsule(), make_fv(file_type=0x09), FMP_GUID, 0)

    def test_rejects_invalid_item_offset(self):
        with self.assertRaisesRegex(ValidationError, "invalid FMP item offset"):
            validate(make_capsule(item_offset=8), make_fv(), FMP_GUID, 0)

    def test_accepts_expected_rmap_regions(self):
        regions = ["EC", "RW_NVRAM", "FW_MAIN_A", "RW_FWID_A", "VBLOCK_A"]
        image = make_rmap_image(regions)
        validate_rmap(image, regions, rmap_firmware(image, len(regions)))

    def test_rejects_rmap_for_different_firmware(self):
        regions = ["EC", "RW_NVRAM", "FW_MAIN_A", "RW_FWID_A", "VBLOCK_A"]
        image = make_rmap_image(regions)
        with self.assertRaisesRegex(ValidationError, "does not match"):
            validate_rmap(image, regions, b"different firmware")

    def test_rejects_missing_or_extra_rmap_regions(self):
        expected = ["EC", "RW_NVRAM", "FW_MAIN_A", "RW_FWID_A", "VBLOCK_A"]
        with self.assertRaisesRegex(ValidationError, "do not match"):
            validate_rmap(make_rmap_image(["EC", "FW_MAIN_A"]), expected)
        with self.assertRaisesRegex(ValidationError, "do not match"):
            validate_rmap(make_rmap_image(expected + ["WP_RO"]), expected)

    def test_rejects_overlapping_rmap_regions(self):
        regions = ["EC", "RW_NVRAM"]
        with self.assertRaisesRegex(ValidationError, "overlapping RMAP"):
            validate_rmap(make_rmap_image(regions, overlap=True), regions)

    def test_accepts_nested_unselected_fmap_regions(self):
        regions = ["EC", "RW_NVRAM", "FW_MAIN_A", "RW_FWID_A", "VBLOCK_A"]
        validate_rmap(make_rmap_image(regions, nested=True), regions)

    def test_rejects_duplicate_or_out_of_bounds_rmap_regions(self):
        with self.assertRaisesRegex(ValidationError, "duplicate RMAP"):
            validate_rmap(make_rmap_image(["EC", "EC"]), ["EC", "EC"])
        with self.assertRaisesRegex(ValidationError, "invalid FMAP"):
            validate_rmap(make_rmap_image(["EC"], out_of_bounds=True), ["EC"])

    def test_rejects_nonempty_rw_nvram(self):
        regions = ["EC", "RW_NVRAM", "FW_MAIN_A", "RW_FWID_A", "VBLOCK_A"]
        image = bytearray(make_rmap_image(regions))
        image[0x500] = 0
        with self.assertRaisesRegex(ValidationError, "RW_NVRAM must be erased"):
            validate_rmap(bytes(image), regions)

    def test_validates_rmap_from_capsule_payload(self):
        regions = ["EC", "RW_NVRAM", "FW_MAIN_A", "RW_FWID_A", "VBLOCK_A"]
        image = make_rmap_image(regions)
        validate(make_capsule(update_image=image, authenticated=True), make_fv(),
                 FMP_GUID, 0, expected_rmap_regions=regions,
                 expected_firmware=rmap_firmware(image, len(regions)))

    def test_rejects_malformed_authenticated_rmap_payload(self):
        regions = ["EC", "RW_NVRAM", "FW_MAIN_A", "RW_FWID_A", "VBLOCK_A"]
        with self.assertRaisesRegex(ValidationError, "authentication header"):
            validate(make_capsule(update_image=make_rmap_image(regions),
                                  authenticated=True, malformed_auth=True), make_fv(),
                     FMP_GUID, 0, expected_rmap_regions=regions)


if __name__ == "__main__":
    unittest.main()
