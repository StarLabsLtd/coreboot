#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read-only check of the original frozen receipts and external readback."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
inputs = (
    ("author-positive", Path("/home/sean/system-fmp-provider-session-final.GCQCjf"), "run-1"),
    ("peer-positive", Path("/home/sean/system-fmp-core-ram-native-peer.PLLBd2"), "run-1"),
    ("failed-provider-alias", Path("/home/sean/system-fmp-core-ram-after570.LYMdWP"), "run-2"),
)
for label, original, run in inputs:
    directory = root / label
    before = json.loads((directory / "inputs-before.json").read_text())
    after = json.loads((directory / "inputs-after.json").read_text())
    assert before == after
    result = json.loads((directory / "result.json").read_text())
    media = (original / run / "pflash.rom").read_bytes()
    initial = Path(next(path for path in before if path.endswith("original-pristine-full-core.rom"))).read_bytes()
    target = Path(next(path for path in before if path.endswith("newer-pristine-full-core.rom"))).read_bytes()
    if label != "failed-provider-alias":
        assert result["failure"] is None and result["qemu_status"] == 3
        assert result["wall_seconds"] <= 180
        assert sum(event.get("event") == "RESET" for event in result["events"]) == 2
        assert media[73728:] == target[73728:]
    else:
        assert result["failure"] is not None
        assert media[73728:] == initial[73728:]
    assert media[65536:73728] == initial[65536:73728]
    retained = (original / run / "boot-1/retained-capsule.bin").read_bytes()
    capsule_path = next(path for path in before if path.endswith("signed-capsule.bin"))
    assert retained == Path(capsule_path).read_bytes()
    print(label, "original before/after maps equal:", len(before),
          "COREBOOT/probe/retained-signed-bytes exact;")
    for name, data in (("final-media", media), ("retained-capsule", retained)):
        print(label, name, hashlib.sha256(data).hexdigest())
