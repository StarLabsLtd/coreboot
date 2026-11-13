#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check native capsule scheduling and output replacement using the production rule.

The signer and files_added hook are deterministic fixtures. This checks the make
dependency boundary, not capsule cryptography or firmware update execution.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap


def native_rule(root):
    marker = ("ifeq ($(CONFIG_PAYLOAD_CDK2)$(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES)"
              "$(CONFIG_DRIVERS_EFI_GENERATE_CAPSULE),yyy)")
    lines = (root / "Makefile.mk").read_text().splitlines(keepends=True)
    starts = [i for i, line in enumerate(lines) if line.strip() == marker]
    assert len(starts) == 1, "native capsule rule must have one owner"
    depth = 0
    for end in range(starts[0], len(lines)):
        words = lines[end].split()
        directive = words[0] if words else ""
        if directive in ("ifeq", "ifneq", "ifdef", "ifndef"):
            depth += 1
        elif directive == "endif":
            depth -= 1
            if depth == 0:
                return "".join(lines[starts[0]:end + 1])
    raise AssertionError("unterminated native capsule rule")


root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="cdk2-capsule-build-") as directory:
    work = Path(directory)
    (work / "production-rule.mk").write_text(native_rule(root))
    (work / "build/util/efi_capsule").mkdir(parents=True)
    (work / "util/efi_capsule").mkdir(parents=True)
    source = work / "util/efi_capsule/capsule.c"
    source.write_text("/* Fixture dependency; the signer is a test double. */\n")
    signer = work / "build/util/efi_capsule/capsule"
    signer.write_text(textwrap.dedent("""\
        #!/usr/bin/env python3
        import json
        from pathlib import Path
        import sys

        args = sys.argv[1:]
        with Path("signer-calls").open("a") as stream:
            stream.write(json.dumps(args) + "\\n")
        output = Path(args[args.index("--output") + 1])
        if Path("fail-signing").exists():
            output.write_text("incomplete replacement")
            sys.exit(1)
        output.write_text(json.dumps({
            "rom": Path(args[args.index("--rom") + 1]).read_text(),
            "guid": args[args.index("--guid") + 1],
        }))
        """))
    signer.chmod(0o755)
    rom = work / "build/coreboot.rom"
    rom.write_text("unfinalized input\n")
    prerequisites = [source, signer, rom]
    for name in ("config", "signer.pem", "intermediate.pem", "root.pem"):
        path = work / name
        path.write_text("fixture only; no cryptographic material\n")
        prerequisites.append(path)
    for path in prerequisites:
        os.utime(path, (1700000000, 1700000000))

    (work / "Makefile").write_text(textwrap.dedent("""\
        obj := build
        objutil := build/util
        DOTCONFIG := config
        CONFIG_PAYLOAD_CDK2 := y
        CONFIG_DRIVERS_EFI_UPDATE_CAPSULES := y
        CONFIG_DRIVERS_EFI_GENERATE_CAPSULE := y
        CONFIG_DRIVERS_EFI_CAPSULE_SIGNER_PRIVATE_CERT := signer.pem
        CONFIG_DRIVERS_EFI_CAPSULE_OTHER_PUBLIC_CERT := intermediate.pem
        CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT := root.pem
        CONFIG_DRIVERS_EFI_MAIN_FW_GUID := target-a
        CONFIG_DRIVERS_EFI_MAIN_FW_VERSION := 1
        CONFIG_DRIVERS_EFI_MAIN_FW_LSV := 1
        CONFIG_LOCALVERSION := fixture
        CONFIG_DRIVERS_EFI_CAPSULE_PACKAGE_REGIONS := COREBOOT
        strip_quotes = $(1)
        .DELETE_ON_ERROR:
        .PHONY: capsule files_added finalised_rom
        capsule::
        include production-rule.mk
        files_added:: build/coreboot.rom
        \tcp finalization-input build/coreboot.rom
        """))

    def make(*args, expected=0):
        environment = os.environ.copy()
        for name in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "GNUMAKEFLAGS"):
            environment.pop(name, None)
        result = subprocess.run(
            ["make", "--no-print-directory", "capsule", *args], cwd=work,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            env=environment)
        assert result.returncode == expected, result.stdout + result.stderr

    capsule = work / "build/coreboot.cap"
    for generation in ("A", "B"):
        value = f"finalized generation {generation}\n"
        (work / "finalization-input").write_text(value)
        make()
        actual = json.loads(capsule.read_text())
        assert actual == {"rom": value, "guid": "target-a"}, actual
    assert len((work / "signer-calls").read_text().splitlines()) == 2

    make("CONFIG_DRIVERS_EFI_MAIN_FW_GUID=target-b")
    assert json.loads(capsule.read_text())["guid"] == "target-b"

    previous = capsule.read_bytes()
    (work / "fail-signing").touch()
    make(expected=2)
    assert capsule.read_bytes() == previous, "failed signer replaced a valid capsule"
    assert not (work / "build/coreboot.cap.tmp").exists()

print("Native capsule finalization, requested identity and atomic replacement: PASS")
