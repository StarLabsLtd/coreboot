# SPDX-License-Identifier: GPL-2.0-only

tests-y += acpigen-test
tests-y += starlabs-ssdt-suspend-test

acpigen-test-srcs += tests/acpi/acpigen-test.c
acpigen-test-srcs += src/acpi/acpigen.c
acpigen-test-srcs += tests/stubs/console.c

starlabs-ssdt-suspend-test-syssrcs += tests/acpi/starlabs-ssdt-suspend.c
starlabs-ssdt-suspend-test-srcs += src/ec/starlabs/merlin/ssdt.c
starlabs-ssdt-suspend-test-srcs += src/acpi/acpigen.c
starlabs-ssdt-suspend-test-srcs += tests/stubs/console.c
starlabs-ssdt-suspend-test-stage := ramstage
starlabs-ssdt-suspend-test-config += CONFIG_STARLABS_ACPI_EFI_OPTION_SMI=1 \
	CONFIG_EC_STARLABS_MERLIN=1 CONFIG_SYSTEM_TYPE_LAPTOP=1 \
	CONFIG_EC_STARLABS_BATTERY_MODEL=\"TEST\" CONFIG_EC_STARLABS_BATTERY_TYPE=\"TEST\" \
	CONFIG_EC_STARLABS_BATTERY_OEM=\"TEST\"
starlabs-ssdt-suspend-test-cflags += -I src/commonlib/bsd/include
starlabs-ssdt-suspend-test-cflags += -I src/mainboard/starlabs/common/include
starlabs-ssdt-suspend-test-cflags += -I src/ec/starlabs/merlin/variants/merlin
