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
RMAP_SIGNATURE = 0x50414D52
RMAP_VERSION = 1
RMAP_ENTRY_SIZE = 16
FMAP_SIGNATURE = b"__FMAP__"
FMAP_HEADER_SIZE = 56
FMAP_AREA_SIZE = 42
FMP_CAPSULE_SUPPORT_AUTHENTICATION = 1
FMP_CAPSULE_SUPPORT_DEPENDENCY = 2
FMP_PAYLOAD_HEADER_SIGNATURE = b"MSS1"
FMP_PAYLOAD_HEADER_SIZE = 16
WIN_CERTIFICATE_UEFI_GUID_SIZE = 24
WIN_CERTIFICATE_REVISION = 0x0200
WIN_CERT_TYPE_EFI_GUID = 0x0EF1
EFI_CERT_TYPE_PKCS7_GUID = uuid.UUID("4aafd29d-68df-49ee-8aa9-347d375665a7")


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
    image_support = 0
    if image_version >= 2:
        image_header_size += 8
    if image_version >= 3:
        image_header_size += 8
    _require(data, image_offset, image_header_size, "FMP image header")

    update_image_size, vendor_code_size = struct.unpack_from(
        "<II", data, image_offset + 24
    )
    if image_version >= 3:
        image_support = struct.unpack_from("<Q", data, image_offset + 40)[0]
    image_size = image_header_size + update_image_size + vendor_code_size
    if image_offset + image_size != len(data):
        raise ValidationError("FMP image size does not match capsule size")

    embedded = [
        data[fmp_offset + item_offsets[i] : fmp_offset + item_offsets[i + 1]]
        for i in range(embedded_count)
    ]
    image_start = image_offset + image_header_size
    return (_guid(data, image_offset + 4), embedded,
            data[image_start : image_start + update_image_size], image_support)


def unwrap_update_image(image, support):
    if support & FMP_CAPSULE_SUPPORT_DEPENDENCY:
        raise ValidationError("FMP capsule dependencies are unsupported")
    if support & ~FMP_CAPSULE_SUPPORT_AUTHENTICATION:
        raise ValidationError("unsupported FMP capsule image support")

    if support & FMP_CAPSULE_SUPPORT_AUTHENTICATION:
        _require(image, 0, 8 + WIN_CERTIFICATE_UEFI_GUID_SIZE,
                 "FMP authentication header")
        certificate_length, revision, certificate_type = struct.unpack_from(
            "<IHH", image, 8
        )
        certificate_guid = _guid(image, 16)
        if (certificate_length < WIN_CERTIFICATE_UEFI_GUID_SIZE or
                revision != WIN_CERTIFICATE_REVISION or
                certificate_type != WIN_CERT_TYPE_EFI_GUID or
                certificate_guid != EFI_CERT_TYPE_PKCS7_GUID):
            raise ValidationError("invalid FMP authentication header")
        _require(image, 8, certificate_length, "FMP authentication certificate")
        image = image[8 + certificate_length:]

    _require(image, 0, FMP_PAYLOAD_HEADER_SIZE, "FMP payload header")
    signature, header_size, _, _ = struct.unpack_from("<4sIII", image)
    if (signature != FMP_PAYLOAD_HEADER_SIGNATURE or
            header_size < FMP_PAYLOAD_HEADER_SIZE or header_size > len(image)):
        raise ValidationError("invalid FMP payload header")
    return image[header_size:]


def parse_rmap(image):
    _require(image, 0, 8, "RMAP trailer")
    signature, version, count = struct.unpack_from("<IHH", image, len(image) - 8)
    if signature != RMAP_SIGNATURE or version != RMAP_VERSION or not count:
        raise ValidationError("missing or invalid RMAP manifest")

    manifest_size = count * RMAP_ENTRY_SIZE + 8
    _require(image, 0, manifest_size, "RMAP manifest")
    manifest_offset = len(image) - manifest_size
    regions = []
    for offset in range(manifest_offset, len(image) - 8, RMAP_ENTRY_SIZE):
        entry = image[offset : offset + RMAP_ENTRY_SIZE]
        name, separator, padding = entry.partition(b"\0")
        if not name or not separator or padding.strip(b"\0"):
            raise ValidationError("invalid RMAP region name")
        try:
            regions.append(name.decode("ascii"))
        except UnicodeDecodeError as error:
            raise ValidationError("non-ASCII RMAP region name") from error
    if len(set(regions)) != len(regions):
        raise ValidationError("duplicate RMAP region")

    return image[:manifest_offset], regions


def parse_fmap(image, offset, image_size=None):
    """Parse the FMAP header at the caller-selected location.

    The generated fmap.fmap is an FMAP table, while the capsule ROM embeds the
    same table at the FMAP area's location.  Keeping the location selection out
    of this parser prevents a decoy signature elsewhere in the ROM from
    becoming the map used for update policy.
    """
    _require(image, offset, FMAP_HEADER_SIZE, "FMAP header")
    signature, major, minor, base, size, fmap_name, count = struct.unpack_from(
        "<8sBBQI32sH", image, offset
    )
    if signature != FMAP_SIGNATURE or major != 1:
        raise ValidationError("invalid FMAP header")
    if image_size is not None and size != image_size:
        raise ValidationError("FMAP size does not match the firmware image")
    _require(image, offset + FMAP_HEADER_SIZE, count * FMAP_AREA_SIZE, "FMAP areas")

    areas = {}
    for index in range(count):
        area_offset = offset + FMAP_HEADER_SIZE + index * FMAP_AREA_SIZE
        start, length, name, _ = struct.unpack_from("<II32sH", image, area_offset)
        name = name.split(b"\0", 1)[0]
        try:
            name = name.decode("ascii")
        except UnicodeDecodeError as error:
            raise ValidationError("non-ASCII FMAP region name") from error
        if not name or name in areas or start + length > size:
            raise ValidationError("invalid FMAP region")
        areas[name] = (start, length)

    return (base, size, fmap_name.rstrip(b"\0"), areas)


def _overlaps(first, second):
    first_start, first_size = first
    second_start, second_size = second
    return first_start < second_start + second_size and second_start < first_start + first_size


def validate_rmap(image, expected_regions, expected_firmware=None,
                  expected_layout=None):
    image, regions = parse_rmap(image)
    if regions != expected_regions:
        raise ValidationError("RMAP regions do not match the vboot update policy")
    if expected_firmware is not None and image != expected_firmware:
        raise ValidationError("RMAP payload does not match the packaged firmware")
    if expected_layout is None:
        raise ValidationError("RMAP validation requires the selected FMAP layout")
    _, _, _, layout_areas = parse_fmap(expected_layout, 0)
    if "FMAP" not in layout_areas:
        raise ValidationError("selected FMAP layout has no FMAP area")
    fmap_offset, fmap_size = layout_areas["FMAP"]
    if fmap_size < FMAP_HEADER_SIZE:
        raise ValidationError("selected FMAP area is too small")
    candidate = parse_fmap(image, fmap_offset, len(image))
    parse_fmap(expected_layout, 0)
    layout_count = struct.unpack_from("<H", expected_layout, 54)[0]
    fmap_table_size = FMAP_HEADER_SIZE + layout_count * FMAP_AREA_SIZE
    _require(image, fmap_offset, fmap_table_size, "firmware FMAP")
    if image[fmap_offset:fmap_offset + fmap_table_size] != expected_layout[:fmap_table_size]:
        raise ValidationError("firmware FMAP does not match the selected layout")
    areas = candidate[3]
    selected = []
    for region in regions:
        if region not in areas:
            raise ValidationError(f"RMAP region '{region}' is not in the FMAP")
        if not areas[region][1]:
            raise ValidationError(f"RMAP region '{region}' is empty")
        selected.append(areas[region])
    for (offset, length), (next_offset, _) in zip(sorted(selected), sorted(selected)[1:]):
        if offset + length > next_offset:
            raise ValidationError("overlapping RMAP regions")
    if "WP_RO" not in areas or not areas["WP_RO"][1]:
        raise ValidationError("selected FMAP layout has no usable WP_RO area")
    for region, extent in zip(regions, selected):
        if _overlaps(extent, areas["WP_RO"]):
            raise ValidationError(f"RMAP region '{region}' overlaps immutable WP_RO")
    if "RW_NVRAM" not in areas:
        raise ValidationError("RMAP policy has no RW_NVRAM region")
    nvram_offset, nvram_size = areas["RW_NVRAM"]
    if image[nvram_offset:nvram_offset + nvram_size] != b"\xff" * nvram_size:
        raise ValidationError("RW_NVRAM must be erased in a vboot capsule")


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


def find_firmware_volumes(data):
    """Return structurally valid firmware volumes contained in a payload."""
    volumes = []
    offset = 0
    while True:
        offset = data.find(FV_SIGNATURE, offset)
        if offset < 0:
            return volumes
        start = offset - 40
        if start >= 0:
            try:
                _require(data, start, 56, "firmware volume header")
                length = struct.unpack_from("<Q", data, start + 32)[0]
                if length and start + length <= len(data):
                    list(parse_ffs_files(data[start:start + length]))
                    volumes.append(data[start:start + length])
            except ValidationError:
                pass
        offset += len(FV_SIGNATURE)


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
             fmp_module_dir=None, expected_rmap_regions=None,
             expected_firmware=None, expected_layout=None, payload=None):
    capsule_guid, embedded, image, image_support = parse_capsule(capsule)
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

    if payload is not None:
        packaged_volumes = find_firmware_volumes(payload)
        if firmware_volume not in packaged_volumes:
            raise ValidationError("expected DXE firmware volume is not in the packaged payload")

    matching_drivers = sum(
        file_guid == expected_guid and file_type == FFS_FILETYPE_DRIVER
        for file_guid, file_type in parse_ffs_files(firmware_volume)
    )
    if matching_drivers > 1 or (not embedded and matching_drivers != 1):
        raise ValidationError(
            f"firmware volume has {matching_drivers} resident FMP drivers "
            f"with GUID {expected_guid}, expected {'0 or 1' if embedded else '1'}"
        )
    if expected_rmap_regions is not None:
        validate_rmap(unwrap_update_image(image, image_support), expected_rmap_regions,
                      expected_firmware, expected_layout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capsule", required=True, help="generated FMP capsule")
    parser.add_argument("--firmware-volume", required=True, help="EDK2 DXE FV")
    parser.add_argument("--guid", required=True, type=uuid.UUID)
    parser.add_argument("--embedded-drivers", required=True, type=int)
    parser.add_argument("--fmp-module-dir", help="FmpDxe module build directory")
    parser.add_argument("--rmap-region", action="append", dest="rmap_regions",
                        help="required RMAP region name")
    parser.add_argument("--firmware-rom", help="firmware image used to build the capsule")
    parser.add_argument("--layout-fmap", help="FMAP generated for the selected board")
    parser.add_argument("--payload", help="payload extracted from the packaged firmware")
    args = parser.parse_args()

    try:
        with open(args.capsule, "rb") as capsule_file:
            capsule = capsule_file.read()
        with open(args.firmware_volume, "rb") as fv_file:
            firmware_volume = fv_file.read()
        if args.rmap_regions and (args.firmware_rom is None or args.layout_fmap is None):
            raise ValidationError("RMAP validation requires the packaged firmware image and layout")
        firmware = None
        if args.firmware_rom is not None:
            with open(args.firmware_rom, "rb") as firmware_file:
                firmware = firmware_file.read()
        layout = None
        if args.layout_fmap is not None:
            with open(args.layout_fmap, "rb") as layout_file:
                layout = layout_file.read()
        payload = None
        if args.payload is not None:
            with open(args.payload, "rb") as payload_file:
                payload = payload_file.read()
        validate(capsule, firmware_volume, args.guid, args.embedded_drivers,
                 args.fmp_module_dir, args.rmap_regions, firmware, layout, payload)
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
