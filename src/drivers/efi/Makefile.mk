# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_DRIVERS_EFI_VARIABLE_STORE),y)
# VariableFormat.h lives in MdeModulePkg, which uefi_2.4 binding omits.
CPPFLAGS_common += -I$(src)/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include
endif

bootblock-$(CONFIG_DRIVERS_EFI_VARIABLE_STORE)	+= efivars.c
romstage-$(CONFIG_DRIVERS_EFI_VARIABLE_STORE)	+= efivars.c
ramstage-$(CONFIG_DRIVERS_EFI_VARIABLE_STORE)	+= efivars.c
smm-$(CONFIG_DRIVERS_EFI_VARIABLE_STORE)	+= efivars.c

ramstage-$(CONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF)	+= capsules.c
ramstage-$(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES)		+= capsules_legacy.c
ramstage-$(CONFIG_DRIVERS_EFI_CAPSULE_DELIVERY_POLICY)	+= capsule_delivery_policy.c

bootblock-$(CONFIG_USE_UEFI_VARIABLE_STORE)	+= option.c
romstage-$(CONFIG_USE_UEFI_VARIABLE_STORE)	+= option.c
ramstage-$(CONFIG_USE_UEFI_VARIABLE_STORE)	+= option.c
smm-$(CONFIG_USE_UEFI_VARIABLE_STORE)	+= option.c

ramstage-$(CONFIG_DRIVERS_EFI_FW_INFO)	+= info.c fw_info.c
smm-$(CONFIG_CAPSULE_PLATFORM_FACTS)	+= fw_info.c

ifeq ($(CONFIG_CAPSULE_BROKER_TRUST_PACKAGE),y)
capsule-trust-pem := $(call strip_quotes,$(CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT))
cbfs-files-y += capsule/trust.der
capsule/trust.der-file := $(obj)/capsule/trust.der
capsule/trust.der-type := raw

.PHONY: capsule-trust-input-check
capsule-trust-input-check:

$(obj)/capsule/trust.der: capsule-trust-input-check $(DOTCONFIG) \
		util/efi_capsule/package_trust.py util/efi_capsule/crypto_policy.py
	mkdir -p $(dir $@)
	python3 util/efi_capsule/package_trust.py --certificate "$(capsule-trust-pem)" \
		--output "$@"
endif
