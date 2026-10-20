# SPDX-License-Identifier: GPL-2.0-only

tests-y += intel-cse-rw-version-option-none-test
tests-y += intel-cse-rw-version-backend-test
tests-y += intel-cse-rw-version-payload-sync-test

intel-cse-rw-version-option-none-test-srcs += tests/soc/intel-cse-rw-version-test.c
intel-cse-rw-version-option-none-test-cflags += -Isrc/soc/intel/common/block/include
intel-cse-rw-version-option-none-test-cflags += -Isrc/soc/intel/cannonlake/include
intel-cse-rw-version-option-none-test-cflags += -Isrc/vendorcode/intel/edk2/UDK2017/MdePkg/Include
intel-cse-rw-version-option-none-test-cflags += -Isrc/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32
intel-cse-rw-version-option-none-test-config += CONFIG_OPTION_BACKEND_NONE=1
intel-cse-rw-version-option-none-test-config += CONFIG_SOC_INTEL_CSE_LITE_SYNC_BY_PAYLOAD=0
intel-cse-rw-version-option-none-test-config += CONFIG_SOC_INTEL_STORE_CSE_FW_VERSION=1
intel-cse-rw-version-option-none-test-config += CONFIG_SOC_INTEL_CSE_HAVE_SPEC_SUPPORT=1
intel-cse-rw-version-option-none-test-config += CONFIG_ME_SPEC=12

$(call copy-test,intel-cse-rw-version-option-none-test,intel-cse-rw-version-backend-test)
intel-cse-rw-version-backend-test-config := CONFIG_OPTION_BACKEND_NONE=0 \
	CONFIG_SOC_INTEL_CSE_LITE_SYNC_BY_PAYLOAD=0 \
	CONFIG_SOC_INTEL_STORE_CSE_FW_VERSION=1 \
	CONFIG_SOC_INTEL_CSE_HAVE_SPEC_SUPPORT=1 CONFIG_ME_SPEC=12

$(call copy-test,intel-cse-rw-version-option-none-test,intel-cse-rw-version-payload-sync-test)
intel-cse-rw-version-payload-sync-test-config := CONFIG_OPTION_BACKEND_NONE=1 \
	CONFIG_SOC_INTEL_CSE_LITE_SYNC_BY_PAYLOAD=1 \
	CONFIG_SOC_INTEL_STORE_CSE_FW_VERSION=1 \
	CONFIG_SOC_INTEL_CSE_HAVE_SPEC_SUPPORT=1 CONFIG_ME_SPEC=12
