#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

TOP = pathlib.Path(__file__).resolve().parents[2]


def run(args, **kwargs):
	return subprocess.run(args, check=True, text=True, **kwargs)


def evaluate(s5):
	with tempfile.TemporaryDirectory() as directory:
		directory = pathlib.Path(directory)
		(directory / "soc").mkdir()
		(directory / "soc" / "iomap.h").write_text(
			"#define PCH_PWRM_BASE_ADDRESS 0xfe000000\n"
			"#define PCH_PWRM_BASE_SIZE 0x10000\n")
		(directory / "time_alarm.asl").write_text(f'''\
#define CONFIG(x) CONFIG_ ## x
#define CONFIG_SOC_INTEL_COMMON_ACPI_TIME_ALARM_S5 {s5}
DefinitionBlock ("", "SSDT", 2, "TEST", "AWACTEST", 0)
{{
\tScope (\\)
\t{{
\t\tName (WATA, 0)
\t\tName (WATS, 0)
\t\tDevice (\\_SB.PCI0)
\t\t{{
\t\t\tName (_HID, "PNP0A08")
\t\t\tDevice (LPCB)
\t\t\t{{
\t\t\t\tName (_HID, "PNP0A05")
\t\t\t\tDevice (RTC) {{ Name (_HID, EisaId ("PNP0B00")) }}
\t\t\t}}
\t\t}}
\t}}
#include "src/soc/intel/common/acpi/time_alarm.asl"
}}
''')
		asl = directory / "time_alarm.iasl"
		with asl.open("w") as output:
			run(["cpp", "-P", f"-I{directory}", "-I.",
			     str(directory / "time_alarm.asl")], cwd=TOP, stdout=output)
		run(["iasl", "-tc", str(asl)], cwd=TOP, stdout=subprocess.DEVNULL)
		output = run(["acpiexec", "-b",
				      "evaluate \\_SB.AWAC._GCP; evaluate \\_SB.AWAC._PRW",
				      str(asl.with_suffix(".aml"))], cwd=TOP, capture_output=True).stdout

	values = re.findall(r"\[Integer\] = [0-9A-F]{8}([0-9A-F]{8})", output)
	expected = (["000000B7", "00000072", "00000004"] if s5 == 0 else
		    ["000001F7", "00000072", "00000005"])
	if values != expected:
		raise RuntimeError(f"unexpected AWAC capability data: {values}")


def main():
	for tool in ("cpp", "iasl", "acpiexec"):
		if shutil.which(tool) is None:
			raise RuntimeError(f"{tool} is required")

	for s5 in (0, 1):
		evaluate(s5)


if __name__ == "__main__":
	try:
		main()
	except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
		print(error, file=sys.stderr)
		sys.exit(1)
