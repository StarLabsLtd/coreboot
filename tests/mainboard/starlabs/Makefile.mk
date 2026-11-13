# SPDX-License-Identifier: GPL-2.0-only

tests-y += gpio-common-test gpio-adl-test gpio-no-smm-test

gpio-common-test-srcs += tests/mainboard/starlabs/gpio-test.c \
	src/mainboard/starlabs/common/mainboard.c
gpio-common-test-cflags += -I src/mainboard/starlabs/adl/include \
	-I src/soc/intel/alderlake/include -I src/soc/intel/common/block/include \
	-Wl,-T,tests/mainboard/starlabs/bootstate.ld
gpio-common-test-config += CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS=1

$(call copy-test,gpio-common-test,gpio-adl-test)
gpio-adl-test-srcs := tests/mainboard/starlabs/gpio-test.c \
	src/mainboard/starlabs/adl/mainboard.c

$(call copy-test,gpio-common-test,gpio-no-smm-test)
gpio-no-smm-test-config := CONFIG_SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS=0

tests-y += straps-rpl-test straps-mtl-test

straps-rpl-test-srcs += tests/mainboard/starlabs/straps-test.c \
	src/lib/gpio.c tests/stubs/console.c tests/stubs/die.c
straps-rpl-test-stage := romstage
straps-rpl-test-cflags += -I src/soc/intel/common/block/include \
	-I src/soc/intel/alderlake/include -I src/mainboard/starlabs/common/include \
	-I src/drivers/intel/fsp2_0/include -I src/vendorcode/intel/fsp/fsp2_0/raptorlake \
	-I src/vendorcode/intel/edk2/edk2-stable202305/MdePkg/Include \
	-I src/vendorcode/intel/edk2/edk2-stable202305/MdePkg/Include/X64 \
	-I src/vendorcode/intel/edk2/edk2-stable202305/IntelFsp2Pkg/Include
straps-rpl-test-config += CONFIG_SOC_INTEL_METEORLAKE=0 CONFIG_DATA_BUS_WIDTH=128 \
	CONFIG_MRC_CHANNEL_WIDTH=16 CONFIG_DIMMS_PER_CHANNEL=2 \
	CONFIG_UDK_VERSION=202305 CONFIG_DISPLAY_FSP_VERSION_INFO=0 CONFIG_FSP_DIMM_INFO=0

straps-mtl-test-srcs += tests/mainboard/starlabs/straps-test.c \
	src/lib/gpio.c tests/stubs/console.c tests/stubs/die.c
straps-mtl-test-stage := romstage
straps-mtl-test-cflags += -I src/soc/intel/common/block/include \
	-I src/soc/intel/meteorlake/include -I src/mainboard/starlabs/starfighter/include
straps-mtl-test-config += CONFIG_SOC_INTEL_METEORLAKE=1
