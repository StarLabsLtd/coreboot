# SPDX-License-Identifier: GPL-2.0-only

tests-y += acpigen-test
tests-y += rtd3-consumer-generator-test

acpigen-test-srcs += tests/acpi/acpigen-test.c
acpigen-test-srcs += src/acpi/acpigen.c
acpigen-test-srcs += tests/stubs/console.c

rtd3-consumer-generator-test-srcs += tests/acpi/rtd3-consumer-generator-test.c
rtd3-consumer-generator-test-srcs += src/acpi/acpigen.c
rtd3-consumer-generator-test-srcs += src/console/vtxprintf.c
rtd3-consumer-generator-test-srcs += src/commonlib/bsd/string.c
rtd3-consumer-generator-test-srcs += src/drivers/usb/acpi/intel_bluetooth.c
rtd3-consumer-generator-test-srcs += src/lib/hexstrtobin.c
rtd3-consumer-generator-test-srcs += src/soc/intel/common/block/acpi/gpio.c
rtd3-consumer-generator-test-srcs += src/soc/intel/common/block/pcie/rtd3/rtd3.c
rtd3-consumer-generator-test-srcs += tests/stubs/console.c
rtd3-consumer-generator-test-cflags += -I src/soc/intel/common/block/include
rtd3-consumer-generator-test-cflags += -I src/soc/intel/meteorlake/include
rtd3-consumer-generator-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_GPIO=1

.PHONY: smm-gpio-aml-test
smm-gpio-aml-test:
	$(Q)tests/acpi/smm-gpio-aml-test.sh $(top) $(obj)/tests/acpi/smm-gpio-aml

.PHONY: rtd3-consumer-aml-test
rtd3-consumer-aml-test: rtd3-consumer-aml-runner-test \
		build-tests/acpi/rtd3-consumer-generator-test
	$(Q)tests/acpi/rtd3-consumer-aml-test.sh $(top) $(obj)/tests/acpi/rtd3-consumer-aml \
		$(testobj)/tests/acpi/rtd3-consumer-generator-test/run

.PHONY: rtd3-consumer-aml-runner-test
rtd3-consumer-aml-runner-test:
	$(Q)tests/acpi/acpiexec-eval-check.py --self-test
