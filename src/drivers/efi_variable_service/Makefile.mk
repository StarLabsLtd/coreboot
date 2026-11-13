## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_ASS),y)

cbfs-files-y += efi_variable_service_fv
efi_variable_service_fv-file := $(obj)/FIXEDMMFV.Fv
efi_variable_service_fv-type := raw
efi_variable_service_fv-compression := $(if $(filter y,$(CONFIG_VBOOT)),lzma,lz4)

$(obj)/FIXEDMMFV.Fv: $(obj)/UEFIPAYLOAD.fd
	@if ! test -f $@; then \
		$(MAKE) -C payloads/external/edk2 FixedMmService \
			EDK2_FIXED_MM_OUT="$(abspath $@)" \
			$(EDK2_PAYLOAD_ARGS); \
	fi
	@test -f $@

endif

ramstage-$(CONFIG_ASS) += ramstage.c ipl.c hob.c
smm-$(CONFIG_ASS) += smi.c private_store.c capsule_store.c
