# SPDX-License-Identifier: GPL-2.0-only

tests-y += efivars-test

efivars-test-srcs += tests/drivers/efivars.c
efivars-test-srcs += src/drivers/efi/efivars.c
efivars-test-srcs += tests/stubs/console.c
efivars-test-srcs += src/commonlib/region.c

efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Pi/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include/

tests-y += intel-smm-gpio-test

intel-smm-gpio-test-stage := smm
intel-smm-gpio-test-srcs += tests/drivers/intel-smm-gpio.c
intel-smm-gpio-test-srcs += src/soc/intel/common/block/smm/gpio.c
intel-smm-gpio-test-config += CONFIG_SOC_INTEL_METEORLAKE=1
intel-smm-gpio-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_GPIO=1
intel-smm-gpio-test-config += CONFIG_SECURITY_FAIL_CLOSED=0
intel-smm-gpio-test-config += CONFIG_PCR_COMMON_IOSF_1_0=0
intel-smm-gpio-test-cflags += -I src/soc/intel/common/block/include
intel-smm-gpio-test-cflags += -I src/soc/intel/meteorlake/include

tests-y += intel-gpio-sbi-test
tests-y += intel-gpio-sbi-fail-closed-test

intel-gpio-sbi-test-srcs += tests/drivers/intel-gpio-sbi-test.c
intel-gpio-sbi-test-srcs += src/soc/intel/common/block/gpio/gpio.c
intel-gpio-sbi-test-srcs += tests/stubs/console.c
intel-gpio-sbi-test-stage := smm
intel-gpio-sbi-test-cflags += -I src/soc/intel/common/block/include
intel-gpio-sbi-test-cflags += -I src/soc/intel/meteorlake/include
intel-gpio-sbi-test-cflags += -I src/soc/intel/common/pch/include
intel-gpio-sbi-test-cflags += -I src/drivers/intel/fsp2_0/include
intel-gpio-sbi-test-cflags += -I src/vendorcode/intel/edk2/edk2-stable202111/MdePkg/Include
intel-gpio-sbi-test-cflags += -I src/vendorcode/intel/edk2/edk2-stable202111/MdePkg/Include/Ia32
intel-gpio-sbi-test-cflags += -I src/vendorcode/intel/edk2/edk2-stable202111/IntelFsp2Pkg/Include
intel-gpio-sbi-test-cflags += -I src/vendorcode/intel/fsp/fsp2_0/raptorlake
intel-gpio-sbi-test-config += CONFIG_PCR_COMMON_IOSF_1_0=0
intel-gpio-sbi-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_GPIO_16BIT_CPU_PORTID=0
intel-gpio-sbi-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_GPIO_PADCFG_PADTOL=0
intel-gpio-sbi-test-config += CONFIG_SECURITY_FAIL_CLOSED=0
intel-gpio-sbi-test-mocks += soc_gpio_get_community
intel-gpio-sbi-test-mocks += pcr_execute_sideband_msg
intel-gpio-sbi-test-mocks += p2sb_dev_is_hidden p2sb_dev_unhide p2sb_dev_hide

$(eval $(call copy-test,intel-gpio-sbi-test,intel-gpio-sbi-fail-closed-test))
intel-gpio-sbi-fail-closed-test-config := $(filter-out CONFIG_SECURITY_FAIL_CLOSED=0, \
	$(intel-gpio-sbi-fail-closed-test-config))
intel-gpio-sbi-fail-closed-test-config += CONFIG_SECURITY_FAIL_CLOSED=1
