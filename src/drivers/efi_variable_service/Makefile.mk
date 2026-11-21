## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_ASS),y)

smm-$(CONFIG_ARCH_SMM_X86_64) += call_64.S entry_64.c
smm-$(CONFIG_SOC_INTEL_COMMON_BLOCK_SMM) += platform_intel.c
ramstage-$(CONFIG_SOC_INTEL_COMMON_BLOCK_SMM) += hob_intel.c

cbfs-files-y += efi_variable_service_fv
efi_variable_service_fv-file := $(obj)/FIXEDMMFV.Fv
efi_variable_service_fv-type := raw
efi_variable_service_fv-compression := $(if $(filter y,$(CONFIG_VBOOT)),lzma,lz4)

ass-capsule-trust := $(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT))
ass-architecture := $(if $(filter y,$(CONFIG_ARCH_SMM_X86_64)),X64,IA32)
ass-workspace := $(abspath $(obj)/ass/$(ass-architecture))
ass-trust-dep := $(if $(filter y,$(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES)),$(filter /%,$(ass-capsule-trust)))
$(obj)/FIXEDMMFV.Fv: $(DOTCONFIG) util/efi_capsule/check-pinned-source.sh \
		src/drivers/efi_variable_service/Makefile.mk src/drivers/efi_variable_service/build-fixed-mm.sh \
		$(ass-trust-dep)
	MAKEFLAGS= MFLAGS= NASM_PREFIX="$(if $(findstring /,$(NASM)),$(dir $(NASM)))" \
		bash src/drivers/efi_variable_service/build-fixed-mm.sh \
		"$(ass-workspace)" "$(abspath $@)" \
		"$(call strip_quotes,$(CONFIG_ASS_SERVICE_REPOSITORY))" \
		"$(call strip_quotes,$(CONFIG_ASS_SERVICE_REVISION))" \
		"$(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES)" "$(ass-capsule-trust)" "$(ass-architecture)"

endif

ramstage-$(CONFIG_ASS) += ramstage.c ipl.c hob.c
smm-$(CONFIG_ASS) += smi.c private_store.c capsule_store.c
