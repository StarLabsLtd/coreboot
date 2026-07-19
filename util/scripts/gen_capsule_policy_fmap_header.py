#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import hashlib
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT OUTPUT", file=sys.stderr)
        return 1

    digest = hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).digest()
    values = ", ".join(f"0x{byte:02x}" for byte in digest)
    header = (
        "#ifndef CAPSULE_POLICY_FMAP_HASH_H\n"
        "#define CAPSULE_POLICY_FMAP_HASH_H\n\n"
        f"#define CAPSULE_POLICY_FMAP_SHA256 {{ {values} }}\n\n"
        "#endif\n"
    )
    pathlib.Path(sys.argv[2]).write_text(header, encoding="ascii")
    return 0


if __name__ == "__main__":
    sys.exit(main())
