# SPDX-License-Identifier: GPL-2.0-only

tests-y += acpigen-test
tests-y += qemu-rsdp-test

acpigen-test-srcs += tests/acpi/acpigen-test.c
acpigen-test-srcs += src/acpi/acpigen.c
acpigen-test-srcs += tests/stubs/console.c

qemu-rsdp-test-srcs += tests/acpi/qemu_rsdp_standalone_test.c
qemu-rsdp-test-stage := ramstage
qemu-rsdp-test-no_test_framework := 1

.PHONY: qemu-rsdp-source-test
qemu-rsdp-source-test:
	$(Q)tests/acpi/qemu_rsdp_source_test.sh

run-tests/acpi/qemu-rsdp-test: qemu-rsdp-source-test
