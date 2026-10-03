#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read-only validation of the finite copied warm-result/hash-map receipts."""
import json
from pathlib import Path

root = Path(__file__).resolve().parent
for name in ("wrong-signer", "signed-byte"):
    case = root / name
    result = json.loads((case / "result.json").read_text())
    assert result["failure"] is None
    assert result["qemu_status"] == 3
    assert result["budget_seconds"] == 180
    assert 0 < result["wall_seconds"] <= 180
    assert result["expected_certified_refusal"] == name
    assert result["expected_capsule_report"] is True
    assert result["completion"] == "guest continuation after certified refusal"
    assert (case / "inputs-before.json").read_bytes() == (case / "inputs-after.json").read_bytes()
    assert sum(event["event"] == "RESET" for event in result["events"]) == 1
    assert "Actual ordinary Core RAM certified refusal continuation PASS (guest success)" in (
        case / "native.log").read_text()
    print(f"{name}: actual guest continuation, fixed budget and immutable input maps PASS")
