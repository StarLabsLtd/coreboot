#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Validate the structure and identity of an FMP capsule."""

import argparse
import pathlib
import struct
import subprocess
import sys
import tempfile
import uuid

try:
    from .crypto_policy import (
        CryptoPolicyError,
        validate_cms_certificates,
        validate_pem_bundle,
    )
except ImportError:
    from crypto_policy import (  # pylint: disable=import-error
        CryptoPolicyError,
        validate_cms_certificates,
        validate_pem_bundle,
    )


FMP_CAPSULE_GUID = uuid.UUID("6dcbd5ed-e82d-4c44-bda1-7194199ad92a")
FV_SIGNATURE = b"_FVH"
FFS_FILETYPE_DRIVER = 0x07
PKCS7_GUID = uuid.UUID("4aafd29d-68df-49ee-8aa9-347d375665a7")
PERSIST_ACROSS_RESET = 0x00010000
INITIATE_RESET = 0x00040000
MSS1_SIGNATURE = b"MSS1"
FMAP_SIGNATURE = b"__FMAP__"
FMAP_HEADER_SIZE = 56
FMAP_AREA_SIZE = 42
FMAP_NAME_SIZE = 32
FMAP_MAX_AREAS = 32
FMAP_AREA_FLAGS_MASK = 0x0F
PAYLOAD_MM_MAX_CMS_SIZE = 256 * 1024
PAYLOAD_MM_MAX_SIGNED_BODY_SIZE = 128 * 1024 * 1024 - 8


class ValidationError(ValueError):
    pass


def _require(data, offset, size, description):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValidationError(f"truncated {description}")


def _guid(data, offset):
    _require(data, offset, 16, "GUID")
    return uuid.UUID(bytes_le=data[offset : offset + 16])


def parse_capsule_details(data):
    _require(data, 0, 28, "capsule header")
    capsule_guid = _guid(data, 0)
    header_size, flags, image_size = struct.unpack_from("<III", data, 16)

    if capsule_guid != FMP_CAPSULE_GUID:
        raise ValidationError(f"not an FMP capsule: {capsule_guid}")
    if header_size != 28:
        raise ValidationError(f"invalid capsule header size: {header_size}")
    if image_size != len(data):
        raise ValidationError(
            f"capsule size is {len(data)}, header declares {image_size}"
        )
    if flags & ~(PERSIST_ACROSS_RESET | INITIATE_RESET):
        raise ValidationError(f"unsupported capsule flags: {flags:#x}")
    if not flags & PERSIST_ACROSS_RESET:
        raise ValidationError("capsule is not persistent across reset")

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
    if item_offsets[0] != item_table_size:
        raise ValidationError("FMP items do not immediately follow the offset list")
    previous = item_table_size - 1
    for item_offset in item_offsets:
        if item_offset <= previous or fmp_offset + item_offset >= len(data):
            raise ValidationError(f"invalid FMP item offset: {item_offset}")
        previous = item_offset

    image_offset = fmp_offset + item_offsets[embedded_count]
    _require(data, image_offset, 32, "FMP image header")
    image_version = struct.unpack_from("<I", data, image_offset)[0]
    if image_version != 3:
        raise ValidationError(f"unsupported FMP image header version: {image_version}")

    image_header_size = 32
    if image_version >= 2:
        image_header_size += 8
    if image_version >= 3:
        image_header_size += 8
    _require(data, image_offset, image_header_size, "FMP image header")
    image_index = data[image_offset + 20]
    if image_index != 1:
        raise ValidationError(f"unsupported update image index: {image_index}")
    if data[image_offset + 21:image_offset + 24] != b"\0" * 3:
        raise ValidationError("FMP image header reserved bytes are not zero")

    update_image_size, vendor_code_size = struct.unpack_from(
        "<II", data, image_offset + 24
    )
    hardware_instance, capsule_support = struct.unpack_from(
        "<QQ", data, image_offset + 32
    )
    if vendor_code_size != 0:
        raise ValidationError("FMP vendor code is not supported")
    if hardware_instance != 0 or capsule_support != 1:
        raise ValidationError("unsupported FMP image identity fields")
    image_size = image_header_size + update_image_size + vendor_code_size
    if image_offset + image_size != len(data):
        raise ValidationError("FMP image size does not match capsule size")

    update_image_offset = image_offset + image_header_size
    update_image_end = update_image_offset + update_image_size
    authenticated = data[update_image_offset:update_image_end]
    _require(authenticated, 0, 32, "authenticated image header")
    monotonic_count = struct.unpack_from("<Q", authenticated, 0)[0]
    certificate_size, certificate_revision, certificate_type = struct.unpack_from(
        "<IHH", authenticated, 8
    )
    if certificate_size <= 24 or certificate_size > len(authenticated) - 8:
        raise ValidationError(f"invalid certificate size: {certificate_size}")
    if certificate_size - 24 > PAYLOAD_MM_MAX_CMS_SIZE:
        raise ValidationError("PKCS#7 signature exceeds the typed broker limit")
    if certificate_revision != 0x0200 or certificate_type != 0x0EF1:
        raise ValidationError("unsupported capsule certificate header")
    if _guid(authenticated, 16) != PKCS7_GUID:
        raise ValidationError("capsule certificate is not PKCS#7")
    payload_offset = 8 + certificate_size
    payload = authenticated[payload_offset:]
    _require(payload, 0, 16, "MSS1 payload header")
    signature, mss1_header_size, fw_version, lsv = struct.unpack_from(
        "<4sIII", payload, 0
    )
    if signature != MSS1_SIGNATURE or mss1_header_size != 16:
        raise ValidationError("invalid MSS1 payload header")
    if not payload[16:]:
        raise ValidationError("MSS1 payload image is empty")
    if len(payload) > PAYLOAD_MM_MAX_SIGNED_BODY_SIZE:
        raise ValidationError("signed payload exceeds the typed broker limit")
    if lsv > fw_version:
        raise ValidationError("lowest-supported version exceeds firmware version")

    return {
        "image_guid": _guid(data, image_offset + 4),
        "embedded_count": embedded_count,
        "flags": flags,
        "fw_version": fw_version,
        "lsv": lsv,
        "monotonic_count": monotonic_count,
        "authenticated": authenticated,
        "payload": payload,
        "image": payload[16:],
        "signature": authenticated[32:payload_offset],
    }


def parse_capsule(data):
    details = parse_capsule_details(data)
    return details["image_guid"], details["embedded_count"]


def parse_rmap(image):
    _require(image, len(image) - 8, 8, "RMAP trailer")
    signature, version, count = struct.unpack_from("<4sHH", image, len(image) - 8)
    if signature != b"RMAP" or version != 1 or count == 0:
        raise ValidationError("invalid RMAP trailer")
    manifest_size = count * 16 + 8
    _require(image, len(image) - manifest_size, manifest_size, "RMAP manifest")
    names = []
    offset = len(image) - manifest_size
    for index in range(count):
        raw = image[offset + index * 16:offset + (index + 1) * 16]
        name = raw.rstrip(b"\0")
        if (not name or b"\0" in name or
                any(byte <= 0x20 or byte > 0x7e for byte in name)):
            raise ValidationError("invalid RMAP region name")
        try:
            decoded = name.decode("ascii")
        except UnicodeDecodeError as error:
            raise ValidationError("non-ASCII RMAP region name") from error
        if decoded in names:
            raise ValidationError(f"duplicate RMAP region: {decoded}")
        names.append(decoded)
    return names, image[:len(image) - manifest_size]


def _padded_name(raw):
    terminator = raw.find(b"\0")
    if (terminator <= 0 or any(raw[terminator:]) or
            any(byte <= 0x20 or byte > 0x7e for byte in raw[:terminator])):
        raise ValidationError("invalid FMAP name")
    return raw[:terminator].decode("ascii")


def _areas_partially_overlap(left, right):
    left_start, left_size = left
    right_start, right_size = right
    left_end = left_start + left_size
    right_end = right_start + right_size
    overlap = left_start < right_end and right_start < left_end
    left_contains = left_start <= right_start and left_end >= right_end
    right_contains = right_start <= left_start and right_end >= left_end
    return overlap and not left_contains and not right_contains


def _parse_fmap_at(image, offset):
    if len(image) - offset < FMAP_HEADER_SIZE:
        return None
    _, major, _, base, size, raw_name, count = struct.unpack_from(
        "<8sBBQI32sH", image, offset
    )
    try:
        _padded_name(raw_name)
    except ValidationError:
        return None
    if (major != 1 or base != 0 or size != len(image) or count == 0 or
            count > FMAP_MAX_AREAS or
            count * FMAP_AREA_SIZE > len(image) - offset - FMAP_HEADER_SIZE):
        return None
    regions = {}
    raw_names = set()
    area_offset = offset + FMAP_HEADER_SIZE
    for index in range(count):
        entry = area_offset + index * FMAP_AREA_SIZE
        start, length, raw_name, flags = struct.unpack_from(
            "<II32sH", image, entry
        )
        try:
            name = _padded_name(raw_name)
        except ValidationError:
            return None
        if (raw_name in raw_names or flags & ~FMAP_AREA_FLAGS_MASK or
                length == 0 or start > size or length > size - start or
                any(_areas_partially_overlap((start, length), region)
                    for region in regions.values())):
            return None
        raw_names.add(raw_name)
        regions[name] = (start, length)
    if "COREBOOT" not in regions or "FMAP" not in regions:
        return None
    fmap_start, fmap_size = regions["FMAP"]
    table_size = FMAP_HEADER_SIZE + count * FMAP_AREA_SIZE
    if (offset < fmap_start or offset - fmap_start > fmap_size or
            table_size > fmap_size - (offset - fmap_start)):
        return None
    return regions


def parse_fmap_regions(image):
    matches = []
    offset = image.find(FMAP_SIGNATURE)
    while offset >= 0:
        regions = _parse_fmap_at(image, offset)
        if regions is not None:
            matches.append(regions)
        offset = image.find(FMAP_SIGNATURE, offset + 1)
    if len(matches) != 1:
        raise ValidationError(
            f"firmware image has {len(matches)} valid FMAP headers"
        )
    return matches[0]


def verify_signature(details, trusted_public_cert):
    signed_content = details["payload"] + struct.pack(
        "<Q", details["monotonic_count"]
    )
    with tempfile.TemporaryDirectory(prefix="coreboot-capsule-verify-") as temporary:
        content = pathlib.Path(temporary, "content.bin")
        signature = pathlib.Path(temporary, "signature.der")
        content.write_bytes(signed_content)
        signature.write_bytes(details["signature"])
        try:
            validate_cms_certificates(details["signature"])
            validate_pem_bundle(trusted_public_cert, "trusted bundle")
            subprocess.run(
                ["openssl", "smime", "-verify", "-binary", "-inform", "DER",
                 "-in", str(signature), "-content", str(content), "-CAfile",
                 trusted_public_cert, "-partial_chain", "-purpose", "any",
                 "-no_check_time", "-out", "/dev/null"],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except (CryptoPolicyError, subprocess.CalledProcessError) as error:
            raise ValidationError("capsule PKCS#7 verification failed") from error


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

        if file_size == 0xffffff:
            if not (file_attributes & 0x01):
                raise ValidationError("large FFS file lacks the large-file attribute")
            _require(data, offset, 32, "large FFS file header")
            file_size = struct.unpack_from("<Q", data, offset + 24)[0]
            header_size = 32

        if file_size < header_size or offset + file_size > fv_length:
            raise ValidationError(f"invalid FFS file size at offset {offset:#x}")

        yield file_guid, file_type
        offset = (offset + file_size + 7) & ~7


def validate(capsule, firmware_volume, expected_guid, expected_embedded_count,
             expected_fw_version=None, expected_lsv=None, expected_image=None,
             expected_regions=None, trusted_public_cert=None,
             expected_initiate_reset=None, expect_rmap=False,
             require_fmap=False):
    details = parse_capsule_details(capsule)
    if details["image_guid"] != expected_guid:
        raise ValidationError(
            f"capsule image GUID {details['image_guid']} does not match {expected_guid}"
        )
    if details["embedded_count"] != expected_embedded_count:
        raise ValidationError(
            f"capsule has {details['embedded_count']} embedded drivers, "
            f"expected {expected_embedded_count}"
        )
    if expected_initiate_reset is not None:
        reset = bool(details["flags"] & INITIATE_RESET)
        if reset != expected_initiate_reset:
            raise ValidationError("capsule reset flag does not match")
    if expected_fw_version is not None and details["fw_version"] != expected_fw_version:
        raise ValidationError("capsule firmware version does not match")
    if expected_lsv is not None and details["lsv"] != expected_lsv:
        raise ValidationError("capsule lowest-supported version does not match")
    if expected_image is not None and details["image"] != expected_image:
        raise ValidationError("capsule payload image does not match")
    if expected_regions is not None:
        rom = details["image"]
        if expect_rmap:
            regions, rom = parse_rmap(rom)
            if regions != expected_regions:
                raise ValidationError("capsule RMAP region list does not match")
        fmap_regions = parse_fmap_regions(rom)
        missing = [region for region in expected_regions if region not in fmap_regions]
        if missing:
            raise ValidationError(f"RMAP region is absent from FMAP: {missing[0]}")
    elif require_fmap:
        parse_fmap_regions(details["image"])
    if trusted_public_cert is not None:
        verify_signature(details, trusted_public_cert)

    if firmware_volume is None:
        return

    matching_drivers = sum(
        file_guid == expected_guid and file_type == FFS_FILETYPE_DRIVER
        for file_guid, file_type in parse_ffs_files(firmware_volume)
    )
    if matching_drivers != 1:
        raise ValidationError(
            f"firmware volume has {matching_drivers} resident FMP drivers "
            f"with GUID {expected_guid}, expected 1"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capsule", required=True, help="generated FMP capsule")
    parser.add_argument("--firmware-volume", help="optional payload DXE FV")
    parser.add_argument("--guid", required=True, type=uuid.UUID)
    parser.add_argument("--embedded-drivers", required=True, type=int)
    parser.add_argument("--fw-version", type=lambda value: int(value, 0))
    parser.add_argument("--lsv", type=lambda value: int(value, 0))
    parser.add_argument("--image")
    parser.add_argument("--region", action="append")
    parser.add_argument("--trusted-public-cert")
    parser.add_argument("--initiate-reset", action="store_true")
    parser.add_argument("--rmap", action="store_true")
    parser.add_argument("--require-fmap", action="store_true")
    args = parser.parse_args()

    try:
        with open(args.capsule, "rb") as capsule_file:
            capsule = capsule_file.read()
        firmware_volume = None
        if args.firmware_volume:
            with open(args.firmware_volume, "rb") as fv_file:
                firmware_volume = fv_file.read()
        expected_image = pathlib.Path(args.image).read_bytes() if args.image else None
        validate(capsule, firmware_volume, args.guid, args.embedded_drivers,
                 args.fw_version, args.lsv, expected_image, args.region,
                 args.trusted_public_cert, args.initiate_reset, args.rmap,
                 args.require_fmap)
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
