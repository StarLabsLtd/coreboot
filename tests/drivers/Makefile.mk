# SPDX-License-Identifier: GPL-2.0-only

tests-y += efivars-test
tests-y += smmstore-variable-test

smmstore-variable-test-srcs += tests/drivers/smmstore-variable.c
smmstore-variable-test-srcs += src/drivers/smmstore/smi.c
smmstore-variable-test-srcs += src/drivers/efi/option.c
smmstore-variable-test-srcs += tests/stubs/console.c
smmstore-variable-test-srcs += src/commonlib/region.c
smmstore-variable-test-stage := smm
smmstore-variable-test-config += CONFIG_SMMSTORE=1 CONFIG_USE_UEFI_VARIABLE_STORE=1 \
	CONFIG_OPTION_BACKEND_NONE=0 CONFIG_USE_CBFS_FILE_OPTION_BACKEND=0
smmstore-variable-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
smmstore-variable-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/

tests-y += efi-option-no-smmstore-test
efi-option-no-smmstore-test-srcs += tests/drivers/smmstore-variable.c
efi-option-no-smmstore-test-srcs += src/drivers/efi/option.c
efi-option-no-smmstore-test-stage := smm
efi-option-no-smmstore-test-config += CONFIG_SMMSTORE=0 CONFIG_USE_UEFI_VARIABLE_STORE=1 \
	CONFIG_OPTION_BACKEND_NONE=0 CONFIG_USE_CBFS_FILE_OPTION_BACKEND=0
efi-option-no-smmstore-test-cflags := $(smmstore-variable-test-cflags)

efivars-test-srcs += tests/drivers/efivars.c
efivars-test-srcs += src/drivers/efi/efivars.c
efivars-test-srcs += tests/stubs/console.c
efivars-test-srcs += src/commonlib/region.c

efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Pi/
efivars-test-cflags += -I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include/
