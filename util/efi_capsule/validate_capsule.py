#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Validate identity consistency between an FMP capsule and its EDK2 payload."""

import argparse
from pathlib import Path
import struct
import sys
import uuid


FMP_CAPSULE_GUID = uuid.UUID("6dcbd5ed-e82d-4c44-bda1-7194199ad92a")
FV_SIGNATURE = b"_FVH"
FFS_FILETYPE_DRIVER = 0x07


class ValidationError(ValueError):
    pass


def _require(data, offset, size, description):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValidationError(f"truncated {description}")


def _guid(data, offset):
    _require(data, offset, 16, "GUID")
    return uuid.UUID(bytes_le=data[offset : offset + 16])


def parse_capsule(data):
    _require(data, 0, 28, "capsule header")
    capsule_guid = _guid(data, 0)
    header_size, _, image_size = struct.unpack_from("<III", data, 16)

    if capsule_guid != FMP_CAPSULE_GUID:
        raise ValidationError(f"not an FMP capsule: {capsule_guid}")
    if header_size < 28 or header_size > len(data):
        raise ValidationError(f"invalid capsule header size: {header_size}")
    if image_size != len(data):
        raise ValidationError(
            f"capsule size is {len(data)}, header declares {image_size}"
        )

    fmp_offset = header_size
    _require(data, fmp_offset, 8, "FMP capsule header")
    version, embedded_count, payload_count = struct.unpack_from(
        "<IHH", data, fmp_offset
    )
    if version != 1:
        raise ValidationError(f"unsupported FMP capsule header version: {version}")
    if payload_count != 1:
        raise ValidationError(f"expected one FMP payload, found {payload_count}")

    item_count = embedded_count + payload_count
    item_table_size = 8 + item_count * 8
    _require(data, fmp_offset, item_table_size, "FMP item offset list")
    item_offsets = struct.unpack_from(f"<{item_count}Q", data, fmp_offset + 8)
    previous = item_table_size - 1
    for item_offset in item_offsets:
        if item_offset <= previous or fmp_offset + item_offset >= len(data):
            raise ValidationError(f"invalid FMP item offset: {item_offset}")
        previous = item_offset

    image_offset = fmp_offset + item_offsets[embedded_count]
    _require(data, image_offset, 32, "FMP image header")
    image_version = struct.unpack_from("<I", data, image_offset)[0]
    if image_version < 1:
        raise ValidationError(f"invalid FMP image header version: {image_version}")

    image_header_size = 32
    if image_version >= 2:
        image_header_size += 8
    if image_version >= 3:
        image_header_size += 8
    _require(data, image_offset, image_header_size, "FMP image header")

    update_image_size, vendor_code_size = struct.unpack_from(
        "<II", data, image_offset + 24
    )
    image_size = image_header_size + update_image_size + vendor_code_size
    if image_offset + image_size != len(data):
        raise ValidationError("FMP image size does not match capsule size")

    embedded = [
        data[fmp_offset + item_offsets[i] : fmp_offset + item_offsets[i + 1]]
        for i in range(embedded_count)
    ]
    return _guid(data, image_offset + 4), embedded


def parse_ffs_files(data):
    _require(data, 0, 56, "firmware volume header")
    fv_length = struct.unpack_from("<Q", data, 32)[0]
    signature = data[40:44]
    attributes = struct.unpack_from("<I", data, 44)[0]
    header_length = struct.unpack_from("<H", data, 48)[0]

    if signature != FV_SIGNATURE:
        raise ValidationError("input is not an EFI firmware volume")
    if fv_length > len(data) or header_length < 56 or header_length > fv_length:
        raise ValidationError("invalid firmware volume length")

    erased = 0xff if attributes & 0x800 else 0x00
    offset = (header_length + 7) & ~7
    while offset + 24 <= fv_length:
        header = data[offset : offset + 24]
        if header == bytes([erased]) * 24:
            break

        file_guid = _guid(data, offset)
        file_type = data[offset + 18]
        file_attributes = data[offset + 19]
        file_size = int.from_bytes(data[offset + 20 : offset + 23], "little")
        header_size = 24

        if file_attributes & 0x01:
            if file_size != 0:
                raise ValidationError("large FFS file has a nonzero size field")
            _require(data, offset, 32, "large FFS file header")
            file_size = struct.unpack_from("<Q", data, offset + 24)[0]
            header_size = 32

        if file_size < header_size or offset + file_size > fv_length:
            raise ValidationError(f"invalid FFS file size at offset {offset:#x}")

        yield file_guid, file_type
        offset = (offset + file_size + 7) & ~7


def validate_embedded_driver(driver, module_dir, expected_guid):
    if module_dir is None:
        raise ValidationError("embedded FMP build metadata is required")

    matches = []
    for inf in Path(module_dir).glob("**/OUTPUT/FmpDxe.inf"):
        defines = False
        guids = []
        for line in inf.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if line.startswith("["):
                defines = line.lower() == "[defines]"
            key, _, value = line.partition("=")
            if defines and key.strip() == "FILE_GUID":
                try:
                    guids.append(uuid.UUID(value.strip()))
                except ValueError as error:
                    raise ValidationError(f"invalid FILE_GUID in {inf}") from error
        if guids == [expected_guid]:
            matches.append(inf.with_suffix(".efi"))

    if len(matches) != 1:
        raise ValidationError("expected one matching embedded FMP build descriptor")
    if matches[0].read_bytes() != driver:
        raise ValidationError("embedded FMP differs from its identified build artifact")


def validate(capsule, firmware_volume, expected_guid, expected_embedded_count,
             fmp_module_dir=None):
    capsule_guid, embedded = parse_capsule(capsule)
    embedded_count = len(embedded)
    if capsule_guid != expected_guid:
        raise ValidationError(
            f"capsule image GUID {capsule_guid} does not match {expected_guid}"
        )
    if embedded_count != expected_embedded_count:
        raise ValidationError(
            f"capsule has {embedded_count} embedded drivers, "
            f"expected {expected_embedded_count}"
        )

    if embedded:
        if embedded_count != 1:
            raise ValidationError("expected at most one embedded FMP driver")
        validate_embedded_driver(embedded[0], fmp_module_dir, expected_guid)

    matching_drivers = sum(
        file_guid == expected_guid and file_type == FFS_FILETYPE_DRIVER
        for file_guid, file_type in parse_ffs_files(firmware_volume)
    )
    if matching_drivers > 1 or (not embedded and matching_drivers != 1):
        raise ValidationError(
            f"firmware volume has {matching_drivers} resident FMP drivers "
            f"with GUID {expected_guid}, expected {'0 or 1' if embedded else '1'}"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capsule", required=True, help="generated FMP capsule")
    parser.add_argument("--firmware-volume", required=True, help="EDK2 DXE FV")
    parser.add_argument("--guid", required=True, type=uuid.UUID)
    parser.add_argument("--embedded-drivers", required=True, type=int)
    parser.add_argument("--fmp-module-dir", help="FmpDxe module build directory")
    args = parser.parse_args()

    try:
        with open(args.capsule, "rb") as capsule_file:
            capsule = capsule_file.read()
        with open(args.firmware_volume, "rb") as fv_file:
            firmware_volume = fv_file.read()
        validate(capsule, firmware_volume, args.guid, args.embedded_drivers,
                 args.fmp_module_dir)
    except (OSError, ValidationError) as error:
        print(f"capsule validation failed: {error}", file=sys.stderr)
        return 1

    print(
        f"capsule validated: GUID {args.guid}, "
        f"embedded drivers {args.embedded_drivers}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
