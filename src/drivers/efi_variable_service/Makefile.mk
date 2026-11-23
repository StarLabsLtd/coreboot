## SPDX-License-Identifier: GPL-2.0-only

ramstage-$(CONFIG_ASS) += ramstage.c
smm-$(CONFIG_ASS) += smi.c private_store.c capsule_store.c

ifeq ($(CONFIG_ASS),y)
ifeq ($(CONFIG_HAVE_PHOENIX_ASS),y)
ramstage-y += ramstage_amd.c
smm-y += platform_amd.c
else
smm-y += platform_intel.c
endif

include $(src)/lib/authvar_sources.mk
smm-y += native_smm.c native_media.c native_variables.c native_capsule.c native_capsule_source.c
smm-y += $(addprefix ../../../,$(authvar_sources))
ifeq ($(CONFIG_VBOOT_SLOTS_RW_AB),y)
$(error Native ASS vboot A/B is not qualified for ROM packaging)
endif
ifeq ($(CONFIG_ASS_FIXED_KEYS),y)
smm-y += native_fixed.c native_fixed_assets.S ../../../src/lib/authvar_fixed_policy.c
$(obj)/smm/drivers/efi_variable_service/native_fixed_assets.o: \
		$(wildcard $(src)/drivers/efi_variable_service/keys/*.esl) \
		$(wildcard $(src)/drivers/efi_variable_service/keys/*.auth)
endif
smm-c-ccopts += -I$(top)/src/lib -I$(top)/src/lib/payload_mm_crypto \
	-isystem $(top)/3rdparty/mbedtls/include -isystem $(top)/3rdparty/mbedtls/library \
	-DPAYLOAD_MM_AUTHVAR_CRYPTO -DMBEDTLS_CONFIG_FILE=\"payload_mm_mbedtls_config.h\"

ifeq ($(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES),y)
ass-capsule-trust := $(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT))
ass-trust-dep := $(filter /%,$(ass-capsule-trust))
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
