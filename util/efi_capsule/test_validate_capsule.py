#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Structural conversion checks; signature authorization is tested by the MM owner."""

import struct
import tempfile
import unittest
import uuid
from pathlib import Path

import validate_capsule as validator


OLD_GUID = uuid.UUID('75b29ff6-e882-41c7-903f-5af89e004113')
NEW_GUID = uuid.UUID('eef5c7c3-124f-406a-87fe-4b58589fd331')


def volume(guids):
    data = bytearray(56)
    struct.pack_into('<Q4sIH', data, 32, 56 + 24 * len(guids), b'_FVH', 0, 56)
    for guid in guids:
        data += guid.bytes_le + b'\0\0\x07\0\x18\0\0\0'
    return bytes(data)


def capsule(firmware, target=OLD_GUID, embedded=b''):
    image = struct.pack('<4sIII', b'MSS1', 32, 0, 0) + target.bytes_le + firmware
    image = struct.pack('<I16sB3xIIQQ', 3, OLD_GUID.bytes_le, 1, len(image), 0, 0, 0) + image
    count = int(bool(embedded))
    offset = 8 + 8 * (count + 1)
    items = (struct.pack('<Q', offset) if count else b'')
    items += struct.pack('<Q', offset + len(embedded))
    body = struct.pack('<IHH', 1, count, 1) + items + embedded + image
    return struct.pack('<16sIII', validator.FMP_CAPSULE_GUID.bytes_le, 28, 0, 28 + len(body)) + body


class ConversionTest(unittest.TestCase):
    def setUp(self):
        self.layout = struct.pack('<8sBBQI32sH', b'__FMAP__', 1, 1, 0, 1024, b'FLASH', 1)
        self.layout += struct.pack('<II32sH', 0, 98, b'FMAP', 0)
        self.firmware = self.layout + b'\xff' * (1024 - len(self.layout))
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        output = Path(self.directory.name) / 'OUTPUT'
        output.mkdir()
        (output / 'FmpDxe.inf').write_text('[Defines]\nFILE_GUID = ' + str(OLD_GUID) + '\n')
        (output / 'FmpDxe.efi').write_bytes(b'embedded driver')

    def validate(self, guids=(NEW_GUID,), embedded=b'', **overrides):
        args = dict(capsule=capsule(self.firmware, embedded=embedded),
                    firmware_volume=volume(guids), expected_guid=OLD_GUID,
                    expected_embedded_count=int(bool(embedded)),
                    fmp_module_dir=self.directory.name,
                    expected_firmware=self.firmware, expected_layout=self.layout,
                    firmware_guid=NEW_GUID)
        args.update(overrides)
        validator.validate(**args)

    def test_conversion(self):
        self.validate()
        self.validate(embedded=b'embedded driver')

    def test_new_resident_is_required_even_with_embedded_driver(self):
        for guids in ((), (OLD_GUID,), (NEW_GUID, NEW_GUID)):
            with self.subTest(guids=guids), self.assertRaises(validator.ValidationError):
                self.validate(guids, embedded=b'embedded driver')

    def test_installed_identity_still_binds_capsule(self):
        with self.assertRaisesRegex(validator.ValidationError, 'authenticated capsule target'):
            self.validate(capsule=capsule(self.firmware, target=NEW_GUID))
        with self.assertRaisesRegex(validator.ValidationError, 'capsule image GUID'):
            self.validate(expected_guid=NEW_GUID)

    def test_complete_image_and_layout_are_required(self):
        for changes in (dict(expected_firmware=None), dict(expected_layout=None),
                        dict(expected_firmware=self.firmware[:-1]),
                        dict(expected_layout=self.layout[:56])):
            with self.subTest(changes=changes.keys()), self.assertRaises(validator.ValidationError):
                self.validate(**changes)


if __name__ == '__main__':
    unittest.main()
