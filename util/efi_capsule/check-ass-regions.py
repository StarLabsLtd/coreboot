#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import re
import sys
from pathlib import Path


def validate(fmap_header, block_size, names, srk_pin):
    fields = {
        key: int(value, 16)
        for key, value in re.findall(
            r"^#define FMAP_SECTION_(\w+_(?:START|SIZE)) (0|0x[0-9a-fA-F]+)$",
            fmap_header, re.MULTILINE
        )
    }

    def region(name):
        start = fields.get(f"{name}_START")
        size = fields.get(f"{name}_SIZE", 0)
        if start is None or not size:
            raise ValueError(f"missing or empty FMAP region {name}")
        return start, start + size

    def blocks(bounds):
        start, end = bounds
        return start // block_size * block_size, -(-end // block_size) * block_size

    def overlaps(left, right):
        return left[0] < right[1] and right[0] < left[1]

    if not block_size or block_size & (block_size - 1):
        raise ValueError("invalid SMMSTORE block size")
    if not 0 < len(names) <= 64:
        raise ValueError("expected between 1 and 64 capsule regions")
    protected = [region("SMMSTORE")]
    if srk_pin:
        protected += [region("SRK_PIN_V1"), blocks(region("FMAP"))]
    selected = []
    for name in names:
        if not re.fullmatch(r"[A-Za-z0-9_]{1,15}", name):
            raise ValueError(f"invalid capsule region name {name}")
        bounds = region(name)
        if any(overlaps(blocks(bounds), entry) for entry in protected):
            raise ValueError(f"capsule region {name} intersects protected storage")
        if any(overlaps(bounds, entry) for entry in selected):
            raise ValueError(f"capsule region {name} overlaps another selected region")
        selected.append(bounds)


if __name__ == "__main__":
    try:
        validate(Path(sys.argv[1]).read_text(), int(sys.argv[2], 0),
                 sys.argv[3].split(), sys.argv[4] == "y")
    except (OSError, ValueError) as error:
        sys.exit(f"ASS capsule region policy: {error}")
