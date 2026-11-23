## SPDX-License-Identifier: GPL-2.0-only

ass-native := $(strip $(CONFIG_ASS_NATIVE) $(ASS_NATIVE_LINK_CHECK))

ifeq ($(CONFIG_ASS),y)

ass-owner := $(obj)/ass/owner
.PHONY: ass-owner-check
$(ass-owner): ass-owner-check
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(if $(ass-native),native,fixed-mm)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

$(obj)/ramstage/drivers/efi_variable_service/ramstage.o: $(ass-owner)
$(obj)/smm/drivers/efi_variable_service/smi.o: $(ass-owner)

ifeq ($(ass-native),)
cbfs-files-y += efi_variable_service_fv
efi_variable_service_fv-file := $(obj)/FIXEDMMFV.Fv
efi_variable_service_fv-type := raw
efi_variable_service_fv-compression := $(if $(filter y,$(CONFIG_VBOOT)),lzma,lz4)
endif

ass-capsule-trust := $(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT))
ass-workspace := $(abspath $(obj)/ass/workspace)
ass-trust-dep := $(if $(filter y,$(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES)),$(filter /%,$(ass-capsule-trust)))
$(obj)/FIXEDMMFV.Fv: $(DOTCONFIG) util/efi_capsule/check-pinned-source.sh \
		src/drivers/efi_variable_service/Makefile.mk src/drivers/efi_variable_service/build-fixed-mm.sh \
		$(ass-trust-dep)
	MAKEFLAGS= MFLAGS= NASM_PREFIX="$(if $(findstring /,$(NASM)),$(dir $(NASM)))" \
		bash src/drivers/efi_variable_service/build-fixed-mm.sh \
		"$(ass-workspace)" "$(abspath $@)" \
		"$(call strip_quotes,$(CONFIG_ASS_SERVICE_REPOSITORY))" \
		"$(call strip_quotes,$(CONFIG_ASS_SERVICE_REVISION))" \
		"$(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES)" "$(ass-capsule-trust)"

endif

ramstage-$(CONFIG_ASS) += ramstage.c
ifeq ($(ass-native),)
ramstage-$(CONFIG_ASS) += ipl.c hob.c
endif
smm-$(CONFIG_ASS) += smi.c private_store.c capsule_store.c

ifeq ($(CONFIG_ASS),y)
ifneq ($(ass-native),)
include $(src)/lib/authvar_sources.mk
smm-y += native_smm.c native_media.c native_variables.c native_capsule.c native_capsule_source.c
smm-y += $(addprefix ../../../,$(authvar_sources))
ifeq ($(CONFIG_VBOOT_SLOTS_RW_AB),y)
$(error Native ASS vboot A/B is not qualified for ROM packaging)
endif
smm-c-ccopts += -DASS_NATIVE_LINK_CHECK
ramstage-c-ccopts += -DASS_NATIVE_LINK_CHECK

ifeq ($(CONFIG_ASS_FIXED_KEYS),y)
ass-fixed-header := $(obj)/ass/fixed_policy.h
smm-y += native_fixed.c
$(obj)/smm/drivers/efi_variable_service/native_fixed.o: $(ass-fixed-header)
endif
smm-c-ccopts += -I$(top)/src/lib -I$(top)/src/lib/payload_mm_crypto \
	-isystem $(top)/3rdparty/mbedtls/include -isystem $(top)/3rdparty/mbedtls/library \
	-DPAYLOAD_MM_AUTHVAR_CRYPTO -DMBEDTLS_CONFIG_FILE=\"payload_mm_mbedtls_config.h\"

ifeq ($(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES),y)
ifeq ($(ass-trust-dep),)
$(error Native ASS capsule verification requires an absolute public trust certificate path)
endif
smm-y += capsule_trust.S native_capsule_platform.c
smm-S-ccopts += -DASS_CAPSULE_TRUST_DER=\"$(absobj)/ass/capsule-trust.der\"
$(obj)/smm/drivers/efi_variable_service/capsule_trust.o: $(obj)/ass/capsule-trust.der

$(obj)/ass/capsule-trust.der: $(DOTCONFIG) $(ass-trust-dep)
	@printf "    X509       %s\n" "$@"
	@mkdir -p $(dir $@)
	openssl x509 -in "$(ass-capsule-trust)" -outform DER -out "$@.tmp"
	mv "$@.tmp" "$@"
endif
endif
endif
