#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Build a single-image, authenticated UEFI FMP capsule."""

import argparse
import pathlib
import struct
import subprocess
import tempfile
import uuid


FMP_CAPSULE_GUID = uuid.UUID("6dcbd5ed-e82d-4c44-bda1-7194199ad92a")
PKCS7_GUID = uuid.UUID("4aafd29d-68df-49ee-8aa9-347d375665a7")
PERSIST_ACROSS_RESET = 0x00010000
INITIATE_RESET = 0x00040000


def sign(payload, signer, intermediates, trusted):
    """Return a detached DER PKCS#7 signature and verify its trust chain."""
    with tempfile.TemporaryDirectory(prefix="coreboot-capsule-") as temporary:
        content = pathlib.Path(temporary, "content.bin")
        signature = pathlib.Path(temporary, "signature.der")
        content.write_bytes(payload)
        command = ["openssl", "smime", "-sign", "-binary", "-outform", "DER",
                   "-md", "sha256", "-signer", signer]
        if intermediates:
            command += ["-certfile", intermediates]
        command += ["-in", str(content), "-out", str(signature)]
        subprocess.run(command, check=True)
        subprocess.run(["openssl", "smime", "-verify", "-binary", "-inform", "DER",
                        "-in", str(signature), "-content", str(content),
                        "-CAfile", trusted, "-partial_chain", "-purpose", "any",
                        "-out", "/dev/null"],
                       check=True)
        return signature.read_bytes()


def build_capsule(image, image_guid, fw_version, lsv, signer, intermediates,
                  trusted, embedded_drivers=(), initiate_reset=False,
                  monotonic_count=0, image_index=1):
    if not 1 <= image_index <= 0xff:
        raise ValueError("image index must be between 1 and 255")
    payload = struct.pack("<4sIII", b"MSS1", 16, fw_version, lsv) + image
    signed_content = payload + struct.pack("<Q", monotonic_count)
    signature = sign(signed_content, signer, intermediates, trusted)
    certificate = struct.pack("<IHH16s", 24 + len(signature), 0x0200, 0x0ef1,
                              PKCS7_GUID.bytes_le) + signature
    authenticated = struct.pack("<Q", monotonic_count) + certificate + payload

    image_header = struct.pack("<I16sB3xIIQQ", 3, image_guid.bytes_le,
                               image_index, len(authenticated), 0, 0, 1)
    items = list(embedded_drivers) + [image_header + authenticated]
    table_size = 8 + 8 * len(items)
    offsets = []
    offset = table_size
    for item in items:
        offsets.append(offset)
        offset += len(item)
    fmp = struct.pack("<IHH", 1, len(embedded_drivers), 1)
    fmp += struct.pack(f"<{len(offsets)}Q", *offsets) + b"".join(items)

    flags = PERSIST_ACROSS_RESET | (INITIATE_RESET if initiate_reset else 0)
    return struct.pack("<16sIII", FMP_CAPSULE_GUID.bytes_le, 28, flags,
                       28 + len(fmp)) + fmp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    parser.add_argument("--output", "-o", required=True)
    parser.add_argument("--guid", required=True, type=uuid.UUID)
    parser.add_argument("--fw-version", required=True, type=lambda x: int(x, 0))
    parser.add_argument("--lsv", required=True, type=lambda x: int(x, 0))
    parser.add_argument("--signer-private-cert", required=True)
    parser.add_argument("--other-public-cert")
    parser.add_argument("--trusted-public-cert", required=True)
    parser.add_argument("--embedded-driver", action="append", default=[])
    parser.add_argument("--initiate-reset", action="store_true")
    parser.add_argument("--monotonic-count", type=lambda x: int(x, 0), default=0)
    parser.add_argument("--update-image-index", type=lambda x: int(x, 0), default=1)
    args = parser.parse_args()
    embedded = [pathlib.Path(path).read_bytes() for path in args.embedded_driver]
    capsule = build_capsule(
        pathlib.Path(args.image).read_bytes(), args.guid, args.fw_version, args.lsv,
        args.signer_private_cert, args.other_public_cert, args.trusted_public_cert,
        embedded, args.initiate_reset, args.monotonic_count, args.update_image_index)
    pathlib.Path(args.output).write_bytes(capsule)


if __name__ == "__main__":
    main()
