# SPDX-License-Identifier: GPL-2.0-only

tests-y += acpigen-test

acpigen-test-srcs += tests/acpi/acpigen-test.c
acpigen-test-srcs += src/acpi/acpigen.c
acpigen-test-srcs += tests/stubs/console.c

.PHONY: acpi-time-alarm-test
acpi-time-alarm-test:
	python3 $(top)/tests/acpi/time-alarm-test.py
