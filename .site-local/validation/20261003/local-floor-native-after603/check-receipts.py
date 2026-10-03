#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read only the copied actual local-floor result and immutable input maps."""
import json
from pathlib import Path

root = Path(__file__).resolve().parent
run = root / "run"
result = json.loads((run / "result.json").read_text())
assert result["failure"] is None and result["qemu_status"] == 3
assert result["expected_local_floor_refusal"] is True
assert result["expected_capsule_report"] is True
assert result["expected_certified_refusal"] is None
assert result["expected_refusal"] is None
assert result["completion"] == "guest continuation after local floor refusal"
assert result["budget_seconds"] == 180 and 0 < result["wall_seconds"] <= 180
assert sum(event["event"] == "RESET" for event in result["events"]) == 1
assert (run / "inputs-before.json").read_bytes() == (run / "inputs-after.json").read_bytes()
assert "RAM capsule local floor refused | 0x8000000000000003" in (
    root / "boot-1" / "cbmem-live.log").read_text()
assert "capsule reset flag does not match" in (
    root / "producer" / "generate-fixtures.log").read_text()
assert (root / "producer" / "validate-fixtures.log").read_text().count(
    "capsule validated: GUID") == 2
print("Actual local floor guest continuation/result/reset/input maps and preserved tool failure: PASS")
