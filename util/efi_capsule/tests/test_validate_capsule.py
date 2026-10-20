#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import struct
import tempfile
from pathlib import Path
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


def make_payload(firmware_volume):
    size = 0x2000
    while size < 72 + 24 + 4 + len(firmware_volume):
        size *= 2
    payload = bytearray(b"\xff" * size)
    payload[:0x1000] = make_fv(OTHER_GUID)
    struct.pack_into("<Q", payload, 32, size)
    offset = 72
    section_header_size = 4 if len(firmware_volume) + 4 < 0xffffff else 8
    section = offset + 24
    file_size = 24 + section_header_size + len(firmware_volume)
    payload[offset + 18] = 0x0b
    if file_size >= 0xffffff:
        payload[offset + 19] = 0x01
        payload[offset + 20 : offset + 23] = b"\0\0\0"
        struct.pack_into("<Q", payload, offset + 24, file_size + 8)
        section += 8
        file_size += 8
    else:
        payload[offset + 20 : offset + 23] = file_size.to_bytes(3, "little")
    if section_header_size == 8:
        payload[section : section + 3] = b"\xff\xff\xff"
        struct.pack_into("<I", payload, section + 4, section_header_size + len(firmware_volume))
    else:
        payload[section : section + 3] = (section_header_size + len(firmware_volume)).to_bytes(3, "little")
    payload[section + 3] = 0x17
    payload[section + section_header_size : section + section_header_size + len(firmware_volume)] = firmware_volume
    return bytes(payload)


def make_elf_payload(payload):
    offset = 0x100
    elf = bytearray(offset + len(payload))
    elf[:16] = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    struct.pack_into("<HHIIIIIHHHHHH", elf, 16, 2, 3, 1, 0, 52, 0, 0,
                     52, 32, 1, 0, 0, 0)
    struct.pack_into("<IIIIIIII", elf, 52, 1, offset, 0, 0, len(payload),
                     len(payload), 7, 0x1000)
    elf[offset:] = payload
    return bytes(elf)


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
        validate(make_capsule(), make_payload(make_fv()), FMP_GUID, 0)

    def test_matching_elf_wrapped_payload_and_resident_driver(self):
        validate(make_capsule(), make_elf_payload(make_payload(make_fv())), FMP_GUID, 0)

    def test_rejects_driver_only_in_unrelated_workspace_volume(self):
        with self.assertRaisesRegex(ValidationError, "0 resident FMP drivers"):
            validate(make_capsule(), make_payload(make_fv(OTHER_GUID)), FMP_GUID, 0)

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
        validate(make_capsule(), make_payload(bytes(fv)), FMP_GUID, 0)
        fv[offset + 20] = 1
        with self.assertRaisesRegex(ValidationError, "nonzero size field"):
            validate(make_capsule(), make_payload(bytes(fv)), FMP_GUID, 0)

    def test_matching_embedded_driver_count(self):
        validate(make_capsule(embedded_count=1), make_payload(make_fv()), FMP_GUID, 1,
                 self.module_dir)

    def test_embedded_only_driver(self):
        validate(make_capsule(embedded_count=1), make_payload(make_fv(OTHER_GUID)), FMP_GUID, 1,
                 self.module_dir)

    def test_rejects_missing_embedded_metadata(self):
        with self.assertRaisesRegex(ValidationError, "metadata is required"):
            validate(make_capsule(embedded_count=1), make_payload(make_fv()), FMP_GUID, 1)

    def test_rejects_embedded_identity_mismatch(self):
        (self.output / "FmpDxe.inf").write_text(
            f"[Defines]\n FILE_GUID = {OTHER_GUID}\n"
        )
        with self.assertRaisesRegex(ValidationError, "matching embedded FMP"):
            validate(make_capsule(embedded_count=1), make_payload(make_fv()), FMP_GUID, 1,
                     self.module_dir)

    def test_rejects_stale_embedded_binary(self):
        (self.output / "FmpDxe.efi").write_bytes(b"old build")
        with self.assertRaisesRegex(ValidationError, "differs from"):
            validate(make_capsule(embedded_count=1), make_payload(make_fv()), FMP_GUID, 1,
                     self.module_dir)

    def test_rejects_duplicate_build_descriptors(self):
        output = self.module_dir / "stale" / "OUTPUT"
        output.mkdir(parents=True)
        (output / "FmpDxe.inf").write_bytes((self.output / "FmpDxe.inf").read_bytes())
        with self.assertRaisesRegex(ValidationError, "one matching embedded FMP"):
            validate(make_capsule(embedded_count=1), make_payload(make_fv()), FMP_GUID, 1,
                     self.module_dir)

    def test_rejects_capsule_image_guid_mismatch(self):
        with self.assertRaisesRegex(ValidationError, "does not match"):
            validate(make_capsule(OTHER_GUID), make_payload(make_fv()), FMP_GUID, 0)

    def test_rejects_embedded_driver_count_mismatch(self):
        with self.assertRaisesRegex(ValidationError, "embedded drivers"):
            validate(make_capsule(embedded_count=1), make_payload(make_fv()), FMP_GUID, 0)

    def test_rejects_missing_resident_driver(self):
        with self.assertRaisesRegex(ValidationError, "0 resident FMP drivers"):
            validate(make_capsule(), make_payload(make_fv(OTHER_GUID)), FMP_GUID, 0)

    def test_rejects_duplicate_resident_driver(self):
        with self.assertRaisesRegex(ValidationError, "2 resident FMP drivers"):
            validate(make_capsule(), make_payload(make_fv(duplicate=True)), FMP_GUID, 0)

    def test_rejects_matching_non_driver_ffs_file(self):
        with self.assertRaisesRegex(ValidationError, "0 resident FMP drivers"):
            validate(make_capsule(), make_payload(make_fv(file_type=0x09)), FMP_GUID, 0)

    def test_rejects_invalid_item_offset(self):
        with self.assertRaisesRegex(ValidationError, "invalid FMP item offset"):
            validate(make_capsule(item_offset=8), make_payload(make_fv()), FMP_GUID, 0)


if __name__ == "__main__":
    unittest.main()
