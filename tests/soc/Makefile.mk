# SPDX-License-Identifier: GPL-2.0-only

tests-y += intel-smm-finalize-board-failure-test
tests-y += intel-smm-finalize-gpio-failure-test
tests-y += intel-smm-finalize-soc-failure-test
tests-y += intel-p2sb-lockdown-test
tests-y += intel-smm-heci-finalize-test
tests-y += intel-pcr-sbi-status-test
tests-y += intel-gpio-lock-readback-test

intel-smm-finalize-board-failure-test-srcs += tests/soc/intel-smm-finalize-test.c
intel-smm-finalize-board-failure-test-srcs += src/soc/intel/common/block/smm/smihandler.c
intel-smm-finalize-board-failure-test-srcs += tests/stubs/console.c
intel-smm-finalize-board-failure-test-stage := smm
intel-smm-finalize-board-failure-test-cflags += -I src/soc/intel/common/block/include
intel-smm-finalize-board-failure-test-cflags += -I src/soc/intel/alderlake/include
intel-smm-finalize-board-failure-test-cflags += -I src/southbridge/intel/common/include
intel-smm-finalize-board-failure-test-cflags += -I src/soc/intel/common/pch/include
intel-smm-finalize-board-failure-test-config += CONFIG_XEON_SP_COMMON_BASE=1
intel-smm-finalize-board-failure-test-config += CONFIG_HAVE_SMI_HANDLER=1
intel-smm-finalize-board-failure-test-config += CONFIG_SPI_FLASH_SMM=0
intel-smm-finalize-board-failure-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS=1
intel-smm-finalize-board-failure-test-config += CONFIG_TEST_BOARD_FAILURE=1
intel-smm-finalize-board-failure-test-config += CONFIG_TEST_GPIO_FAILURE=0
intel-smm-finalize-board-failure-test-config += CONFIG_TEST_SOC_FAILURE=0
intel-smm-finalize-board-failure-test-mocks += mainboard_smi_finalize
intel-smm-finalize-board-failure-test-mocks += smihandler_soc_at_finalize
intel-smm-finalize-board-failure-test-mocks += soc_gpio_lock_config gpio_lock_pads
intel-smm-finalize-board-failure-test-mocks += soc_gpio_lock_finalize
intel-smm-finalize-board-failure-test-mocks += enable_smm_bios_protection
intel-smm-finalize-board-failure-test-mocks += opal_s3_smi_apmc apm_get_apmc
intel-smm-finalize-board-failure-test-mocks += pmc_disable_pm1_control pmc_enable_pm1_control
intel-smm-finalize-board-failure-test-mocks += mainboard_smi_apmc

$(eval $(call copy-test,intel-smm-finalize-board-failure-test,\
	intel-smm-finalize-gpio-failure-test))
intel-smm-finalize-gpio-failure-test-config := $(filter-out \
	CONFIG_TEST_BOARD_FAILURE=1 CONFIG_TEST_GPIO_FAILURE=0, \
	$(intel-smm-finalize-gpio-failure-test-config))
intel-smm-finalize-gpio-failure-test-config += CONFIG_TEST_BOARD_FAILURE=0
intel-smm-finalize-gpio-failure-test-config += CONFIG_TEST_GPIO_FAILURE=1

$(eval $(call copy-test,intel-smm-finalize-board-failure-test,\
	intel-smm-finalize-soc-failure-test))
intel-smm-finalize-soc-failure-test-config := $(filter-out \
	CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS=1 \
	CONFIG_TEST_BOARD_FAILURE=1 CONFIG_TEST_SOC_FAILURE=0, \
	$(intel-smm-finalize-soc-failure-test-config))
intel-smm-finalize-soc-failure-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS=0
intel-smm-finalize-soc-failure-test-config += CONFIG_TEST_BOARD_FAILURE=0
intel-smm-finalize-soc-failure-test-config += CONFIG_TEST_SOC_FAILURE=1

intel-p2sb-lockdown-test-srcs += tests/soc/intel-p2sb-lockdown-test.c
intel-p2sb-lockdown-test-srcs += src/soc/intel/common/block/p2sb/p2sb.c
intel-p2sb-lockdown-test-srcs += tests/stubs/console.c
intel-p2sb-lockdown-test-stage := smm
intel-p2sb-lockdown-test-cflags += -I src/soc/intel/common/block/include
intel-p2sb-lockdown-test-cflags += -I src/soc/intel/alderlake/include
intel-p2sb-lockdown-test-cflags += -include tests/soc/p2sb-pci-mock.h
intel-p2sb-lockdown-test-config += CONFIG_PCI=1
intel-p2sb-lockdown-test-mocks += p2sb_soc_get_sb_mask

intel-smm-heci-finalize-test-srcs += tests/soc/intel-smm-heci-finalize-test.c
intel-smm-heci-finalize-test-srcs += src/soc/intel/common/block/smm/smihandler.c
intel-smm-heci-finalize-test-srcs += src/soc/intel/jasperlake/smihandler.c
intel-smm-heci-finalize-test-srcs += src/soc/intel/common/block/cse/disable_heci.c
intel-smm-heci-finalize-test-srcs += src/soc/intel/common/block/p2sb/p2sb.c
intel-smm-heci-finalize-test-srcs += tests/stubs/console.c
intel-smm-heci-finalize-test-srcs += tests/stubs/die.c
intel-smm-heci-finalize-test-stage := smm
intel-smm-heci-finalize-test-cflags += -I src/soc/intel/common/block/include
intel-smm-heci-finalize-test-cflags += -I src/soc/intel/jasperlake/include
intel-smm-heci-finalize-test-cflags += -I src/soc/intel/common/pch/include
intel-smm-heci-finalize-test-cflags += -I src/drivers/intel/fsp2_0/include
intel-smm-heci-finalize-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include
intel-smm-heci-finalize-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32
intel-smm-heci-finalize-test-cflags += -I src/vendorcode/intel/fsp/fsp2_0/jasperlake
intel-smm-heci-finalize-test-cflags += -include tests/soc/p2sb-pci-mock.h
intel-smm-heci-finalize-test-config += CONFIG_XEON_SP_COMMON_BASE=1
intel-smm-heci-finalize-test-config += CONFIG_HAVE_SMI_HANDLER=1
intel-smm-heci-finalize-test-config += CONFIG_PCI=1
intel-smm-heci-finalize-test-config += CONFIG_SPI_FLASH_SMM=0
intel-smm-heci-finalize-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS=0
intel-smm-heci-finalize-test-config += CONFIG_HECI_DISABLE_USING_SMM=1
intel-smm-heci-finalize-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_HECI1_DISABLE_USING_SBI=1
intel-smm-heci-finalize-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_HECI1_DISABLE_USING_PMC_IPC=0
intel-smm-heci-finalize-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_HECI1_DISABLE_USING_PCR=0
intel-smm-heci-finalize-test-mocks += mainboard_smi_finalize
intel-smm-heci-finalize-test-mocks += enable_smm_bios_protection
intel-smm-heci-finalize-test-mocks += opal_s3_smi_apmc apm_get_apmc
intel-smm-heci-finalize-test-mocks += pmc_disable_pm1_control pmc_enable_pm1_control
intel-smm-heci-finalize-test-mocks += mainboard_smi_apmc soc_disable_heci1_at_pre_boot
intel-smm-heci-finalize-test-mocks += pcr_execute_sideband_msg p2sb_soc_get_sb_mask
intel-smm-heci-finalize-test-mocks += p2sb_dev_unhide p2sb_dev_hide
intel-smm-heci-finalize-test-mocks += post_code

intel-pcr-sbi-status-test-srcs += tests/soc/intel-pcr-sbi-status-test.c
intel-pcr-sbi-status-test-srcs += src/soc/intel/common/block/pcr/pcr.c
intel-pcr-sbi-status-test-srcs += tests/stubs/console.c
intel-pcr-sbi-status-test-stage := smm
intel-pcr-sbi-status-test-cflags += -I src/soc/intel/common/block/include
intel-pcr-sbi-status-test-cflags += -I src/soc/intel/jasperlake/include
intel-pcr-sbi-status-test-cflags += -include tests/soc/p2sb-pci-mock.h
intel-pcr-sbi-status-test-config += CONFIG_PCI=1
intel-pcr-sbi-status-test-config += CONFIG_PCR_BASE_ADDRESS=0xfd000000
intel-pcr-sbi-status-test-config += CONFIG_PCR_COMMON_IOSF_1_0=0
intel-pcr-sbi-status-test-config += CONFIG_HAVE_MONOTONIC_TIMER=0

intel-gpio-lock-readback-test-srcs += tests/soc/intel-gpio-lock-readback-test.c
intel-gpio-lock-readback-test-srcs += src/soc/intel/common/block/gpio/gpio.c
intel-gpio-lock-readback-test-srcs += tests/stubs/console.c
intel-gpio-lock-readback-test-srcs += tests/stubs/die.c
intel-gpio-lock-readback-test-stage := smm
intel-gpio-lock-readback-test-cflags += -I src/soc/intel/common/block/include
intel-gpio-lock-readback-test-cflags += -I src/soc/intel/jasperlake/include
intel-gpio-lock-readback-test-cflags += -I src/soc/intel/common/pch/include
intel-gpio-lock-readback-test-cflags += -I src/drivers/intel/fsp2_0/include
intel-gpio-lock-readback-test-cflags += -I src/vendorcode/intel/edk2/edk2-stable202111/MdePkg/Include
intel-gpio-lock-readback-test-cflags += -I src/vendorcode/intel/edk2/edk2-stable202111/MdePkg/Include/Ia32
intel-gpio-lock-readback-test-cflags += -I src/vendorcode/intel/edk2/edk2-stable202111/IntelFsp2Pkg/Include
intel-gpio-lock-readback-test-cflags += -I src/vendorcode/intel/fsp/fsp2_0/jasperlake
intel-gpio-lock-readback-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS=1
intel-gpio-lock-readback-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_GPIO_LOCK_USING_PCR=1
intel-gpio-lock-readback-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_GPIO_LOCK_USING_SBI=0
intel-gpio-lock-readback-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_GPIO_16BIT_CPU_PORTID=0
intel-gpio-lock-readback-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_GPIO_PADCFG_PADTOL=0
intel-gpio-lock-readback-test-config += CONFIG_DEBUG_GPIO=0
intel-gpio-lock-readback-test-mocks += soc_gpio_get_community
intel-gpio-lock-readback-test-mocks += pcr_read32 pcr_or32
intel-gpio-lock-readback-test-mocks += p2sb_unhide p2sb_hide
