# SPDX-License-Identifier: GPL-2.0-only

override EDK2_CAPSULE_PAYLOAD_REGION := FW_MAIN_A

ifneq ($(CONFIG_EDK2_SECURE_BOOT_SUPPORT),y)
$(error Star Labs VBOOT requires EDK2 Secure Boot support)
endif
starlabs-vboot-custom-params := $(subst --define=, ,$(subst --pcd=, ,\
	$(subst ',,$(call strip_quotes,$(CONFIG_EDK2_CUSTOM_BUILD_PARAMS)))))
starlabs-vboot-custom-policy := $(filter SECURE_BOOT_ENABLE=% CAPSULE_SUPPORT=% \
	CAPSULE_MAIN_FW_GUID=% \
	gUefiPayloadPkgTokenSpaceGuid.PcdDefaultSecureBootEnable=% \
	gEfiMdeModulePkgTokenSpaceGuid.PcdTpm2MeasurementRequired=%, \
	$(subst -D, ,$(starlabs-vboot-custom-params)))
ifneq ($(filter-out SECURE_BOOT_ENABLE=TRUE CAPSULE_SUPPORT=TRUE \
	CAPSULE_MAIN_FW_GUID=$(call strip_quotes,$(CONFIG_DRIVERS_EFI_MAIN_FW_GUID)) \
	gUefiPayloadPkgTokenSpaceGuid.PcdDefaultSecureBootEnable=TRUE \
	gEfiMdeModulePkgTokenSpaceGuid.PcdTpm2MeasurementRequired=TRUE, \
	$(starlabs-vboot-custom-policy)),)
$(error Star Labs VBOOT payload parameters must preserve measured Secure Boot)
endif
ifneq ($(or $(findstring FMP_DEVICE_PKCS7_PCD_INC,$(starlabs-vboot-custom-params)), \
	$(findstring PcdFmpDevicePkcs7CertBufferXdr,$(starlabs-vboot-custom-params))),)
$(error Star Labs VBOOT capsule trust must come from the configured public certificate)
endif
ifneq ($(findstring ImageVerificationPolicy,$(starlabs-vboot-custom-params)),)
$(error Star Labs VBOOT uses the required EDK2 image verification policies)
endif
ifeq ($(CAPSULE_LAYOUT_TRANSITION),1)
ifeq ($(shell printf '%s' '$(CAPSULE_GUID)' | tr A-F a-f),$(shell printf '%s' '$(FIRMWARE_GUID)' | tr A-F a-f))
$(error A layout-transition capsule must target the installed non-vboot GUID)
endif
else
ifneq ($(shell printf '%s' '$(CAPSULE_GUID)' | tr A-F a-f),$(shell printf '%s' '$(FIRMWARE_GUID)' | tr A-F a-f))
$(error Star Labs VBOOT capsule GUID must match the configured firmware GUID)
endif
ifeq ($(CONFIG_VBOOT_SLOTS_RW_AB),y)
ifneq ($(CAPSULE_REGIONS),EC)
ifneq ($(CAPSULE_REGIONS),FW_MAIN_A RW_FWID_A VBLOCK_A FW_MAIN_B RW_FWID_B VBLOCK_B)
$(error Star Labs A/B VBOOT capsules must contain EC only or both signed RW slot tuples)
endif
endif
else
ifneq ($(CAPSULE_REGIONS),EC RW_NVRAM FW_MAIN_A RW_FWID_A VBLOCK_A)
$(error Star Labs VBOOT capsules must update EC RW_NVRAM FW_MAIN_A RW_FWID_A VBLOCK_A)
endif
endif
endif
ifneq ($(CAPSULE_LOCALVERSION),$(COREBOOT_LOCALVERSION))
$(error Star Labs VBOOT capsule version must match CONFIG_LOCALVERSION)
endif

# Reserve the release values after the caller-supplied build parameters.
override EDK2_RELEASE_BUILD_ARGS += -D SECURE_BOOT_ENABLE=TRUE
override EDK2_RELEASE_BUILD_ARGS += --pcd gUefiPayloadPkgTokenSpaceGuid.PcdDefaultSecureBootEnable=TRUE
override EDK2_RELEASE_BUILD_ARGS += --pcd gEfiMdeModulePkgTokenSpaceGuid.PcdTpm2MeasurementRequired=TRUE
override EDK2_RELEASE_BUILD_ARGS += --pcd gEfiSecurityPkgTokenSpaceGuid.PcdOptionRomImageVerificationPolicy=0x04
override EDK2_RELEASE_BUILD_ARGS += --pcd gEfiSecurityPkgTokenSpaceGuid.PcdFixedMediaImageVerificationPolicy=0x04
override EDK2_RELEASE_BUILD_ARGS += --pcd gEfiSecurityPkgTokenSpaceGuid.PcdRemovableMediaImageVerificationPolicy=0x04
ifeq ($(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES),y)
override EDK2_RELEASE_BUILD_ARGS += -D CAPSULE_SUPPORT=TRUE
override EDK2_RELEASE_BUILD_ARGS += -D CAPSULE_MAIN_FW_GUID=$(CONFIG_DRIVERS_EFI_MAIN_FW_GUID)
override EDK2_RELEASE_BUILD_ARGS += -D FMP_DEVICE_PKCS7_PCD_INC=$(CAPSULE_FMP_PCD_INC_REL)
endif
