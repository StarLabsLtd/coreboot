## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_ASS),y)

cbfs-files-y += efi_variable_service_fv
efi_variable_service_fv-file := $(obj)/FIXEDMMFV.Fv
efi_variable_service_fv-type := raw
efi_variable_service_fv-compression := $(if $(filter y,$(CONFIG_VBOOT)),lzma,lz4)

ifeq ($(CONFIG_PAYLOAD_EDK2),y)
$(obj)/FIXEDMMFV.Fv: $(obj)/UEFIPAYLOAD.fd
	@if ! test -f $@; then \
		$(MAKE) -C payloads/external/edk2 FixedMmService \
			EDK2_FIXED_MM_OUT="$(abspath $@)" \
			$(EDK2_PAYLOAD_ARGS); \
	fi
	@test -f $@
else
ifeq ($(CONFIG_CDK2_SECURE_BOOT_FIXED_KEYS),y)
ass_fixed_header := $(obj)/ass/fixed_policy.h
.PHONY: ass-fixed-policy
$(ass_fixed_header): ass-fixed-policy $(CDK2_PAYLOAD) src/drivers/efi_variable_service/Makefile.mk
	mkdir -p $(dir $@)
	$(MAKE) -C $(CDK2_SOURCE) fixed-policy-header \
		COREBOOT_SOURCE="$(abspath .)" \
		COREBOOT_CONFIG="$(abspath $(DOTCONFIG))" \
		COREBOOT_CONFIG_HEADER="$(abspath $(obj)/config.h)" \
		CDK2_BUILD_DIR="$(abspath $(CDK2_OUTPUT))" \
		CDK2_KCONFIG_TOOL="$(abspath $(objutil)/kconfig/conf)" \
		CDK2_FIXED_POLICY_HEADER="$(abspath $@)" HOSTCC="$(HOSTCC)"
ass_fixed_args := ASS_FIXED_POLICY_ROOT="$(abspath .)" \
	ASS_FIXED_POLICY_HEADER="$(abspath $(ass_fixed_header))"
endif
.PHONY: ass-service
$(obj)/FIXEDMMFV.Fv: ass-service $(DOTCONFIG) payloads/external/edk2/Makefile \
		src/drivers/efi_variable_service/Makefile.mk \
		$(ass_fixed_header) \
		$(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT))
	mkdir -p $(obj)/ass
	$(MAKE) -C payloads/external/edk2 FixedMmService \
		WORKSPACE="$(abspath $(obj)/ass/workspace)" \
		$(ass_fixed_args) \
		EDK2_FIXED_MM_OUT="$(abspath $@)" $(EDK2_PAYLOAD_ARGS) \
		CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT="$(if $(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT)),$(abspath $(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT))))"
	@test -f $@
endif

endif

ramstage-$(CONFIG_ASS) += ramstage.c ipl.c hob.c
smm-$(CONFIG_ASS) += smi.c private_store.c capsule_store.c
