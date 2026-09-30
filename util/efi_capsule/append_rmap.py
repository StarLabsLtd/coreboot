#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Append a region allow-list (RMAP) trailer to a firmware image."""

import argparse
import pathlib
import struct


def build_manifest(regions):
    if not regions or len(regions) > 0xffff:
        raise ValueError("RMAP requires between 1 and 65535 regions")
    encoded = []
    for region in regions:
        name = region.encode("ascii")
        if not name or len(name) > 16 or any(byte <= 0x20 or byte > 0x7e for byte in name):
            raise ValueError(f"invalid RMAP region name: {region!r}")
        if name in encoded:
            raise ValueError(f"duplicate RMAP region: {region}")
        encoded.append(name)
    return b"".join(name.ljust(16, b"\0") for name in encoded) + struct.pack(
        "<4sHH", b"RMAP", 1, len(encoded))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    parser.add_argument("--output", "-o", required=True)
    parser.add_argument("--region", "-r", action="append", required=True)
    args = parser.parse_args()
    pathlib.Path(args.output).write_bytes(
        pathlib.Path(args.image).read_bytes() + build_manifest(args.region))


if __name__ == "__main__":
    main()
