# SPDX-License-Identifier: GPL-2.0-only

tests-y += smi-trigger-common-no-gpio-test
tests-y += smi-trigger-common-gpio-test
tests-y += smi-trigger-common-no-gpio-fail-closed-test
tests-y += smi-trigger-common-gpio-fail-closed-test
tests-y += smi-trigger-legacy-test

smi-trigger-common-no-gpio-test-srcs += tests/cpu/smi-trigger-test.c
smi-trigger-common-no-gpio-test-srcs += src/cpu/x86/smi_trigger.c
smi-trigger-common-no-gpio-test-srcs += tests/stubs/console.c
smi-trigger-common-no-gpio-test-config += CONFIG_HAVE_SMI_HANDLER=1
smi-trigger-common-no-gpio-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_SMM=1
smi-trigger-common-no-gpio-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS=0
smi-trigger-common-no-gpio-test-config += CONFIG_SECURITY_FAIL_CLOSED=0
smi-trigger-common-no-gpio-test-mocks += apm_control_call_smm die

$(eval $(call copy-test,smi-trigger-common-no-gpio-test,smi-trigger-common-gpio-test))
smi-trigger-common-gpio-test-config := $(filter-out \
	CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS=0, \
	$(smi-trigger-common-gpio-test-config))
smi-trigger-common-gpio-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS=1

$(eval $(call copy-test,smi-trigger-common-no-gpio-test,\
	smi-trigger-common-no-gpio-fail-closed-test))
smi-trigger-common-no-gpio-fail-closed-test-config := $(filter-out \
	CONFIG_SECURITY_FAIL_CLOSED=0, \
	$(smi-trigger-common-no-gpio-fail-closed-test-config))
smi-trigger-common-no-gpio-fail-closed-test-config += CONFIG_SECURITY_FAIL_CLOSED=1

$(eval $(call copy-test,smi-trigger-common-gpio-test,\
	smi-trigger-common-gpio-fail-closed-test))
smi-trigger-common-gpio-fail-closed-test-config := $(filter-out \
	CONFIG_SECURITY_FAIL_CLOSED=0, \
	$(smi-trigger-common-gpio-fail-closed-test-config))
smi-trigger-common-gpio-fail-closed-test-config += CONFIG_SECURITY_FAIL_CLOSED=1

$(eval $(call copy-test,smi-trigger-common-no-gpio-test,smi-trigger-legacy-test))
smi-trigger-legacy-test-config := $(filter-out \
	CONFIG_SOC_INTEL_COMMON_BLOCK_SMM=1, \
	$(smi-trigger-legacy-test-config))
smi-trigger-legacy-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_SMM=0
