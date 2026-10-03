#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read only the exact traced HOST A/B receipts and recorded compiler argv."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
HEAD = "127c80df85986ca77ce50f3bfcb6fc98c3ac41dc"
REASONS = {
    "coreboot-only": "show_linear_status(&context, CDK2_LVGL_STATUS_PREPARING) == EFI_SUCCESS",
    "begin-error": "status_calls == 1 && received.message == CDK2_LVGL_STATUS_SELECTING_BOOT",
    "debug-only": "status_calls == 1 && received.message == CDK2_LVGL_STATUS_SELECTING_BOOT",
    "reseal": "allocations == saved_allocations",
    "dirty-bgrt": "BGRT excludes transient status",
    "owner-generation": "captured UI generation refusal",
    "callback-identity": "captured UI callback refusal",
}
EXPECTED = [
    "original", "coreboot-only", "begin-error",
    "original", "coreboot-only", "begin-error", "debug-only",
    "original", "coreboot-only", "begin-error", "owner-generation", "callback-identity",
    "original", "coreboot-only", "begin-error", "reseal", "dirty-bgrt",
    "owner-generation", "callback-identity",
]


def receipt(variant, profile):
    output = ROOT / f"{variant}-p{profile}"
    assert (output / "outer.status").read_text().strip() == "0", output
    assert (output / "source-head.txt").read_text().strip() == HEAD, output
    for manifest in ("source-before.sha256", "inputs-before.sha256"):
        subprocess.run(["sha256sum", "--quiet", "-c", str(output / manifest)], check=True)
    lines = (output / "gate.log").read_text().splitlines()
    modes = []
    outcomes = []
    active = None
    for line in lines:
        mode = re.fullmatch(r"\+ \[ ([a-z-]+) != original \]", line)
        if mode:
            active = mode.group(1)
            modes.append(active)
        elif line == "+ status=134":
            assert active in REASONS, line
            outcomes.append((active, 134))
        elif line.startswith("+ grep -Fq "):
            assert line.startswith("+ grep -Fq " + REASONS[active] + " "), line
    assert modes == EXPECTED, (output, modes)
    assert outcomes == [(mode, 134) for mode in EXPECTED if mode != "original"], output
    assert sum(line.startswith("+ ASAN_OPTIONS=") and line.endswith("/report") for line in lines) == 19
    assert sum(line.startswith("+ cmp ") and not line.startswith("+ cmp -s ")
               for line in lines) == 3
    assert sum(line == "Splash owner/begin-sentinel/snapshot lifetime coupled source mutants: PASS"
               for line in lines) == 1
    match = re.fullmatch(r"WALL=([0-9.]+) USER=([0-9.]+) SYS=([0-9.]+) PEAK_KIB=(\d+) EXIT=0",
                         (output / "gate.time").read_text().strip())
    assert match, output
    return output, modes, float(match.group(1))


def compiler_records(output, support_count):
    artifacts = list((output / "tmp").glob("tmp.*"))
    assert len(artifacts) == 1, artifacts
    artifacts = artifacts[0]
    sources = None
    fixtures = 0
    for debug in (0, 1):
        for optimization in (0, 2):
            raw = (artifacts / f"argv-{debug}-{optimization}").read_bytes()
            assert raw.endswith(b"\0\0"), artifacts
            records = [[item.decode() for item in record.split(b"\0")]
                       for record in raw[:-2].split(b"\0\0")]
            deps = [args for args in records if "-M" in args]
            objects = [args for args in records if "-c" in args]
            links = [args for args in records if "-Wl,--gc-sections" in args]
            expected_links = {(0, 0): 3, (0, 2): 4, (1, 0): 5, (1, 2): 7}[debug, optimization]
            assert len(deps) == 1 and len(objects) == support_count
            assert len(links) == expected_links and len(records) == 1 + support_count + expected_links
            current_sources = [args[args.index("-c") + 1] for args in objects]
            if sources is None:
                sources = current_sources
            assert current_sources == sources
            dependency_sources = [item for item in deps[0] if item.endswith(".c")]
            assert dependency_sources[0].endswith("/tests/splash_status_report_test.c")
            assert dependency_sources[1:] == sources
            expected_objects = [str(artifacts / f"support-{debug}-{optimization}" /
                                    f"support-{index:04d}.o")
                                for index in range(1, support_count + 1)]
            for args in records:
                assert args[0] == "cc" and "-std=c11" in args
                assert f"-O{optimization}" in args and f"-DCDK2_SPLASH_TEST_DEBUG={debug}" in args
                assert "-fsanitize=address,undefined" in args and "-fno-sanitize-recover=all" in args
                assert "-fno-pie" in args
            for index, args in enumerate(objects):
                assert args[args.index("-o") + 1] == expected_objects[index]
                assert "-no-pie" not in args and "-Wl,--gc-sections" not in args
            for args in links:
                assert "-no-pie" in args
                assert [item for item in args if item.endswith(".o")] == expected_objects
                assert sum(item.endswith("/tests/splash_status_report_test.c") for item in args) == 1
                assert sum(item.startswith("-DCDK2_SPLASH_ENTRY_SOURCE=") for item in args) == 1
                fixtures += 1
            subprocess.run(["sha256sum", "--quiet", "-c", str(artifacts /
                f"support-{debug}-{optimization}" / "inputs.sha256")], check=True)
    assert fixtures == 19
    print(f"P{profile}: four complete dependency manifests verified; support {support_count} x 4; fixture links 19")


for profile in ([int(sys.argv[1])] if len(sys.argv) == 2 else (0, 1)):
    baseline, baseline_modes, old_time = receipt("baseline", profile)
    candidate, candidate_modes, new_time = receipt("candidate", profile)
    assert baseline_modes == candidate_modes
    compiler_records(candidate, {0: 30, 1: 44}[profile])
    print(f"P{profile}: original4 + precise134/noSAN15 + inverseTU3 + finalPASS1; "
          f"traced named wall {old_time:.2f}s -> {new_time:.2f}s "
          f"({100 * (1 - new_time / old_time):.2f}% less)")
print("Exact final620 HOST A/B receipts: PASS (no firmware/native UI or persistent-cache claim)")
