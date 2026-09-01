#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import struct
import unittest
import uuid

from util.efi_capsule.validate_capsule import (
    FMP_CAPSULE_GUID,
    ValidationError,
    validate,
)


FMP_GUID = uuid.UUID("85584837-7b03-4b6c-940c-8b6186cbf7a1")
OTHER_GUID = uuid.UUID("83335bd5-ec9e-4a78-8931-223d990f5b44")


def make_capsule(image_guid=FMP_GUID, embedded_count=0, item_offset=None):
    embedded_items = [b"MZ" for _ in range(embedded_count)]
    image = struct.pack(
        "<I16sB3xIIQQ",
        3,
        image_guid.bytes_le,
        1,
        1,
        0,
        0,
        0,
    ) + b"\0"
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


class ValidateCapsuleTest(unittest.TestCase):
    def test_matching_capsule_and_resident_driver(self):
        validate(make_capsule(), make_fv(), FMP_GUID, 0)

    def test_transition_capsule_and_resident_driver(self):
        validate(
            make_capsule(OTHER_GUID),
            make_fv(),
            FMP_GUID,
            0,
            OTHER_GUID,
        )

    def test_matching_embedded_driver_count(self):
        validate(make_capsule(embedded_count=1), make_fv(), FMP_GUID, 1)

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


if __name__ == "__main__":
    unittest.main()
