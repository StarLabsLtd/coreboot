# SPDX-License-Identifier: GPL-2.0-only
"""Read-only checks of finite saved receipts; no VM or omitted-media replay."""
import hashlib
import io
import json
import re
import subprocess
import tarfile
from pathlib import Path

root = Path(__file__).resolve().parent
cases = {"wrong-signer": "DEVICE_ERROR (0x8000000000000007)",
         "signed-byte": "DEVICE_ERROR (0x8000000000000007)",
         "below-floor": "UNSUPPORTED (0x8000000000000003)"}
markers = tuple("CDK2_CORE_RAM_STAGE_" + suffix for suffix in
                ("MAIN_ENTRY", "SETUP1_SECURE0", "REAL_SYSTEM_FMP_INFO",
                 "ACTUAL_INITIAL_ZERO_STATE", "QUERY_STRUCTURAL_ONLY",
                 "UPDATE_CAPSULE_RESET"))
for case, status in cases.items():
    directory = root / "native" / case
    run = directory / "run-1"
    result = json.loads((run / "result.json").read_text())
    assert result["failure"] is None
    assert result["expected_refusal"] == case and result["refusal_observed"] is True
    assert result["qemu_status"] == 0
    assert result["completion"] == "HOST termination after validated refusal"
    assert result["budget_seconds"] == 180
    assert sum(event["event"] == "RESET" and event.get("data", {}).get("guest") is True
               for event in result["events"]) == 1
    before = json.loads((run / "inputs-before.json").read_text())
    after = json.loads((run / "inputs-after.json").read_text())
    assert before == after
    serial = (run / "serial.log").read_bytes()
    for marker in markers:
        assert serial.count(marker.encode()) == 1, marker
    assert b"CDK2_CORE_RAM_ACTUAL_NORMAL_CORE_COLD_RUNNING_AND_HISTORY_PASS" not in serial
    assert b"CDK2_CORE_RAM_STAGE_ACTUAL_PRIOR_SUCCESS_STATE" not in serial
    assert b"CDK2_CORE_RAM_STAGE_FAIL" not in serial
    warm = (run / "boot-1" / "cbmem-live.log").read_text()
    ram = warm[warm.rfind("CDK2 | CAPSULE_RAM | begin"):]
    assert ram.count("CDK2 | CAPSULE_RAM | failed | " + status) == 1
    assert "CDK2 | PCI_ROOTS | begin" not in ram
    assert not re.search(r"CDK2 \| CAPSULE_RAM \| (end|done|complete)", ram)
    config = (directory / "resolved.config").read_text().splitlines()
    assert "CONFIG_CDK2_NATIVE_SYSTEM_FMP=y" in config
    assert "CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME=y" in config
    assert "CONFIG_CDK2_NATIVE_QEMU_TEST_FMP=y" not in config
    assert "CONFIG_CDK2_QEMU_ACCEPTANCE_PROFILE=y" not in config
    print(case + ": copied status/reset/markers/input maps PASS")

for name, table in (("consumer-66bd", "consumer"),
                    ("producer-codecs-b06", "producer")):
    expected = {}
    for line in (root / "source" / (table + "-blobs.tsv")).read_text().splitlines():
        metadata, path = line.split("\t", 1)
        mode, kind, digest = metadata.split()
        assert kind == "blob" and mode in ("100644", "100755")
        expected[path] = digest
    archive = subprocess.check_output(["zstd", "-q", "-d", "-c",
                                     str(root / "source" / (name + ".tar.zst"))])
    seen = set()
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for member in source:
            if member.isdir():
                continue
            assert member.isfile() and member.name in expected
            body = source.extractfile(member).read()
            digest = hashlib.sha1(b"blob " + str(len(body)).encode() + b"\0" + body).hexdigest()
            assert digest == expected[member.name], member.name
            seen.add(member.name)
    assert seen == set(expected)
    print(name + ": all " + str(len(seen)) + " signed source blobs PASS")
print("Saved receipt checks only; no omitted media, fresh VM or continuation claim.")
