## SPDX-License-Identifier: GPL-2.0-only

ass-native := $(filter y,$(CONFIG_ASS_NATIVE))

ifeq ($(CONFIG_ASS),y)

ifeq ($(ass-native),)
ifneq ($(CONFIG_PAYLOAD_EDK2)$(CONFIG_EDK2_REPO_CUSTOM),yy)
$(error Fixed MM requires its existing compatible payload configuration)
endif

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
endif

ass-owner := $(obj)/ass/owner
.PHONY: ass-owner-check
$(ass-owner): ass-owner-check
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(if $(ass-native),native,fixed-mm)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

$(obj)/ramstage/drivers/efi_variable_service/ramstage.o: $(ass-owner)
$(obj)/smm/drivers/efi_variable_service/smi.o: $(ass-owner)

ramstage-$(CONFIG_ASS) += ramstage.c
ifeq ($(ass-native),)
ramstage-$(CONFIG_ASS) += ipl.c hob.c
endif
smm-$(CONFIG_ASS) += smi.c private_store.c capsule_store.c

ass-capsule-trust := $(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT))
ass-trust-dep := $(filter /%,$(ass-capsule-trust))

ifeq ($(CONFIG_ASS),y)
ifneq ($(ass-native),)
ramstage-$(CONFIG_SOC_AMD_PHOENIX_OPENSIL) += hob_amd.c
smm-$(CONFIG_SOC_AMD_PHOENIX_OPENSIL) += platform_amd.c
ifeq ($(CONFIG_VBOOT_SLOTS_RW_AB),y)
ifneq ($(CONFIG_VBOOT_VBNV_FLASH):$(CONFIG_VBOOT_VBNV_CMOS),y:)
$(error Native ASS vboot A/B requires flash as the primary VBNV backend)
endif
ifneq ($(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES),y)
$(error Native ASS vboot A/B requires the capsule owner for boot acknowledgement)
endif
ifeq ($(CONFIG_VBOOT_MARK_BOOT_SUCCESSFUL_AFTER_BOOTMEDIA_LOCKDOWN),y)
$(error Native ASS vboot A/B acknowledges boot success at the payload handoff)
endif
ifneq ($(CONFIG_ASS_NATIVE_LATE_EXIT),y)
$(error Native ASS vboot A/B requires a payload with late trial acknowledgement)
endif
endif
include $(src)/lib/authvar_sources.mk
smm-y += native_smm.c native_media.c native_variables.c native_capsule.c native_capsule_source.c
smm-y += $(addprefix ../../../,$(authvar_sources))
smm-c-ccopts += -DASS_NATIVE_LINK_CHECK
ramstage-c-ccopts += -DASS_NATIVE_LINK_CHECK

ifeq ($(CONFIG_ASS_FIXED_KEYS),y)
ass-fixed-header := $(call strip_quotes,$(CONFIG_ASS_FIXED_POLICY_HEADER))
ifeq ($(ass-fixed-header),)
$(error Native ASS fixed keys require a public key data header)
endif
smm-c-ccopts += -DASS_FIXED_POLICY_HEADER=\"$(abspath $(ass-fixed-header))\"
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
