## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_ASS),y)

ass-owner := $(obj)/ass/owner
.PHONY: ass-owner-check
$(ass-owner): ass-owner-check
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(if $(ASS_NATIVE_LINK_CHECK),native,fixed-mm)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

$(obj)/ramstage/drivers/efi_variable_service/ramstage.o: $(ass-owner)
$(obj)/smm/drivers/efi_variable_service/smi.o: $(ass-owner)

ifeq ($(ASS_NATIVE_LINK_CHECK),)
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
ifeq ($(ASS_NATIVE_LINK_CHECK),)
ramstage-$(CONFIG_ASS) += ipl.c hob.c
endif
smm-$(CONFIG_ASS) += smi.c private_store.c capsule_store.c

ifeq ($(CONFIG_ASS),y)
ifneq ($(ASS_NATIVE_LINK_CHECK),)
include $(src)/lib/authvar_sources.mk
smm-y += native_smm.c native_media.c native_variables.c native_capsule.c native_capsule_source.c
smm-y += $(addprefix ../../../,$(authvar_sources))
# Local link checkpoint until the native owner also replaces the ramstage loader.
ifeq ($(CONFIG_VBOOT_SLOTS_RW_AB),y)
$(error Native ASS vboot A/B is not qualified for ROM packaging)
endif
smm-c-ccopts += -DASS_NATIVE_LINK_CHECK
ramstage-c-ccopts += -DASS_NATIVE_LINK_CHECK

ifeq ($(CONFIG_PAYLOAD_EDIIE)$(CONFIG_EDIIE_SECURE_BOOT_FIXED_KEYS),yy)
ass-fixed-header := $(obj)/ass/fixed_policy.h
ass-ediie := $(top)/payloads/external/ediie/ediie
ass-dbx-baseline := $(ass-ediie)/src/modules/bds/keys/dbx_microsoft_update.bin
ass-dbx-append := $(ass-ediie)/src/modules/bds/keys/dbx_microsoft_append.esl
ass-dbx-update := $(ass-ediie)/src/modules/variable_runtime/keys/fixed_policy_dbx_update.bin
smm-y += native_fixed.c native_fixed_assets.S
smm-S-ccopts += -DASS_DBX_BASELINE=\"$(ass-dbx-baseline)\" \
	-DASS_DBX_APPEND=\"$(ass-dbx-append)\" -DASS_DBX_UPDATE=\"$(ass-dbx-update)\"
$(obj)/smm/drivers/efi_variable_service/native_fixed.o: $(ass-fixed-header)
$(obj)/smm/drivers/efi_variable_service/native_fixed_assets.o: \
	$(ass-dbx-baseline) $(ass-dbx-append) $(ass-dbx-update)
endif
smm-c-ccopts += -I$(top)/src/lib -I$(top)/src/lib/payload_mm_crypto \
	-isystem $(top)/3rdparty/mbedtls/include -isystem $(top)/3rdparty/mbedtls/library \
	-DPAYLOAD_MM_AUTHVAR_CRYPTO -DMBEDTLS_CONFIG_FILE=\"payload_mm_mbedtls_config.h\"

ifeq ($(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES),y)
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
