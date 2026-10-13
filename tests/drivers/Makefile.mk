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

tests-y += efi-private-store-test
efi-private-store-test-srcs += tests/drivers/efi-private-store.c
efi-private-store-test-srcs += src/drivers/efi_variable_service/private_store.c
efi-private-store-test-srcs += src/drivers/efi_variable_service/capsule_store.c
efi-private-store-test-srcs += src/commonlib/region.c

tests-y += smmstore-srk-test smmstore-srk-customer-test
$(foreach test,smmstore-srk-test smmstore-srk-customer-test,\
	$(eval $(test)-srcs += tests/drivers/smmstore-srk.c src/drivers/smmstore/store.c \
		src/drivers/smmstore/smi.c src/commonlib/region.c tests/stubs/console.c)\
	$(eval $(test)-stage := smm)\
	$(eval $(test)-config += CONFIG_SMMSTORE_SRK_PIN=1 CONFIG_SMMSTORE_BLOCK_SIZE=0x10000 \
		CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=1 CONFIG_ROM_SIZE=0x40000)\
	$(eval $(test)-cflags += -I tests/drivers/smmstore-srk-include \
		-I src/soc/intel/common/block/include))
smmstore-srk-test-config += CONFIG_SMMSTORE_SRK_PROVISION=1
smmstore-srk-customer-test-config += CONFIG_SMMSTORE_SRK_PROVISION=0

tests-y += smmstore-fixed-mm-test
smmstore-fixed-mm-test-srcs += tests/drivers/smmstore-fixed-mm.c src/drivers/smmstore/smi.c \
	src/commonlib/region.c tests/stubs/console.c
smmstore-fixed-mm-test-stage := smm
smmstore-fixed-mm-test-config += CONFIG_ASS=1 CONFIG_SMMSTORE_BLOCK_SIZE=0x10000

tests-y += efi-variable-service-init-test
efi-variable-service-init-test-srcs += tests/drivers/efi-variable-service-init.c \
	src/drivers/efi/efivars.c src/drivers/efi_variable_service/private_store.c \
	src/commonlib/region.c tests/stubs/console.c
efi-variable-service-init-test-stage := smm
efi-variable-service-init-test-config += CONFIG_ASS=1 CONFIG_SMMSTORE_BLOCK_SIZE=0x10000 \
	CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=0 CONFIG_ROM_SIZE=0x80000
efi-variable-service-init-test-cflags += -I tests/drivers/efi-variable-service-include \
	-I src/soc/intel/common/block/include -I src/soc/intel/alderlake/include \
	-I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include \
	-I src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/Ia32 \
	-I src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include
