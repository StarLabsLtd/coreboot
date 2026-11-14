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
$(obj)/FIXEDMMFV.Fv: $(DOTCONFIG) payloads/external/edk2/Makefile \
		src/drivers/efi_variable_service/Makefile.mk \
		$(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT))
	mkdir -p $(obj)/ass
	$(MAKE) -C payloads/external/edk2 FixedMmService \
		WORKSPACE="$(abspath $(obj)/ass/workspace)" \
		EDK2_FIXED_MM_OUT="$(abspath $@)" $(EDK2_PAYLOAD_ARGS)
	@test -f $@
endif

endif

ramstage-$(CONFIG_ASS) += ramstage.c ipl.c hob.c
smm-$(CONFIG_ASS) += smi.c private_store.c capsule_store.c
