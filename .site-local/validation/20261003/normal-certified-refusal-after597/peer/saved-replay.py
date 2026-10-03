# SPDX-License-Identifier: GPL-2.0-only
"""Read-only saved native continuation opposition; no VM or guest writes."""
import hashlib
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

source = Path("/home/sean/Documents/.cdk2-worktrees/certified-refusal-continuation-peer-after595.3Qdgg0")
root = Path("/home/sean/normal-refusal-consumed-producer.VPNIdB")
sys.path.insert(0, str(source / "tests"))
spec = importlib.util.spec_from_file_location("runner", source / "tests/system_fmp_core_ram_native_test.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
assert subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"]).decode().strip() == "31e3a5bae19db98457bfbd61fb1b37ed6b0ade52"
for case, capsule in [("wrong-signer", "wrong-signer-a.cap"), ("signed-byte", "signed-byte-corrupt-a.cap")]:
    run = root / case / "run-1"
    result = json.loads((run / "result.json").read_text())
    assert result["failure"] is None and result["qemu_status"] == 3
    assert result["refusal_observed"] is True
    assert result["completion"] == "guest continuation after certified refusal"
    assert result["budget_seconds"] == 180 and result["wall_seconds"] <= 180
    before = json.loads((run / "inputs-before.json").read_text())
    assert before == json.loads((run / "inputs-after.json").read_text())
    # Runtime maps retain original paths. Re-read all still-frozen source and
    # immutable donor inputs; no map entry is replaced by learned media bytes.
    for name, expected in before.items():
        assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == expected, name
    boots = sorted(run.glob("boot-*"))
    runner.check_boots(boots, (root / capsule).stat().st_size,
                      refusal=case, certified=True)
    runner.check_refusal_markers((run / "serial.log").read_bytes(), result["events"], True)
    assert (run / "boot-1/retained-capsule.bin").read_bytes() == (root / capsule).read_bytes()
    runner.check_refusal_media((run / "pflash.rom").read_bytes(),
                              (root / "initial9/build/coreboot.rom").read_bytes(),
                              {"SMMSTORE": (0, 65536)})
    assert runner.digest(run / "pflash.rom") == result["installed_sha256"]
    assert runner.digest(run / "nvme.raw") == result["disk_sha256"]
    assert runner.loads(run / "initial-core.elf") == runner.loads(Path("/home/sean/capsule-linear-completion-normal.fw344b/native/cdk2-coreboot-image.elf"))
    assert runner.loads(run / "target-core.elf") == runner.loads(Path("/home/sean/capsule-linear-completion-normal.fw344b/native/cdk2-coreboot-image.elf"))
    print(f"PASS saved {case}: exact two FW9 epochs, one guest warm reset, certified RAM complete before PCI, ordinary MAIN request absence/zero-state marker, retained candidate identity, input maps/current hashes, loaded Core, outside-SMMSTORE equality")
    print(f"actual QEMU seconds={result['wall_seconds']}; disk hash matches recorded final and original runner enforces pre/post disk equality")
print("Saved-only opposition PASS; not a fresh VM, Linux, hardware or full-tree regression proof.")
