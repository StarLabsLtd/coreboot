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
