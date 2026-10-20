#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import sys


def reset_operations(text):
    start = text.find("Method (_RST")
    if start < 0:
        raise ValueError("missing _RST method")

    body = text[start:]
    depth = 0
    opened = False
    lines = []
    for line in body.splitlines():
        depth += line.count("{")
        if "{" in line:
            opened = True
        if any(token in line for token in ("PCTX", "PSTX", "BTRK", "Sleep (RDLY)",
                                             "Release (\\_SB.PCI0.CNMT)")):
            lines.append(line.strip())
        depth -= line.count("}")
        if opened and depth == 0:
            break
    return lines


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ("plain", "optional"):
        raise ValueError("usage: checker DSL plain|optional")
    with open(sys.argv[1], encoding="utf-8") as source:
        actual = reset_operations(source.read())

    expected = ["\\_SB.PCI0.BTRK (Zero)", "Sleep (RDLY)",
                "\\_SB.PCI0.BTRK (One)", "Sleep (RDLY)",
                "Release (\\_SB.PCI0.CNMT)"]
    if sys.argv[2] == "optional":
        expected[0:0] = ["\\_SB.PCI0.PCTX (0x0D)", "\\_SB.PCI0.PCTX (0x0E)"]
        expected[4:4] = ["\\_SB.PCI0.PSTX (0x0D)", "\\_SB.PCI0.PSTX (0x0E)"]

    if actual != expected:
        raise ValueError(f"unexpected _RST sequence: {actual!r}")


try:
    main()
except (OSError, ValueError) as error:
    print(f"Bluetooth reset sequence rejected: {error}", file=sys.stderr)
    sys.exit(1)
