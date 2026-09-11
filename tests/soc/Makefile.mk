# SPDX-License-Identifier: GPL-2.0-only

tests-y += intel-vtd-test

intel-vtd-test-srcs += tests/soc/intel-vtd-test.c
intel-vtd-test-srcs += tests/stubs/console.c
intel-vtd-test-config += CONFIG_ENABLE_EARLY_DMA_PROTECTION=1
intel-vtd-test-cflags += -I src/soc/intel/common/block/include
intel-vtd-test-cflags += -I src/drivers/intel/fsp2_0/include
intel-vtd-test-cflags += -I src/soc/intel/alderlake/include
intel-vtd-test-cflags += -I src/vendorcode/intel/edk2/edk2-stable202305/MdePkg/Include
intel-vtd-test-cflags += -I src/vendorcode/intel/edk2/edk2-stable202305/MdePkg/Include/Ia32
intel-vtd-test-cflags += -I src/vendorcode/intel/fsp/fsp2_0/alderlake
intel-vtd-test-cflags += -I src/vendorcode/intel/edk2/edk2-stable202305/IntelFsp2Pkg/Include
intel-vtd-test-stage := romstage
