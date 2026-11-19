## SPDX-License-Identifier: GPL-2.0-only

ifneq ($(CONFIG_ASS),y)
$(error edIIe requires coreboot ASS)
endif
ifeq ($(CONFIG_DRIVERS_EFI_UPDATE_CAPSULES),y)
ifneq ($(CONFIG_DRIVERS_EFI_CAPSULE_DISK_ONLY)$(CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT),yy)
$(error edIIe firmware updates require capsule-on-disk only)
endif
ediie-capsule-policy := $(obj)/ediie-capsule-policy.checked
$(ediie-capsule-policy): $(obj)/fmap_config.h $(DOTCONFIG) util/efi_capsule/check-ass-regions.py
	python3 util/efi_capsule/check-ass-regions.py "$<" $(CONFIG_SMMSTORE_BLOCK_SIZE) \
		$(CONFIG_DRIVERS_EFI_CAPSULE_REGIONS) "$(CONFIG_SMMSTORE_SRK_PIN)"
	touch "$@"
endif

EDIIE_SOURCE := payloads/external/ediie/ediie
EDIIE_OUTPUT := $(obj)/ediie
EDIIE_PAYLOAD := $(EDIIE_OUTPUT)/native/ediie-coreboot-image.elf
override CONFIG_PAYLOAD_FILE := $(EDIIE_PAYLOAD)

# binutils built only for ELF may lack the PE emulation used by the modules.
EDIIE_NATIVE_LD ?= $(shell command -v ld)
.PHONY: ediie-source-check
ediie-source-check:
	@sh payloads/external/ediie/verify-source.sh "$(EDIIE_SOURCE)"

$(EDIIE_PAYLOAD): ediie-source-check $(DOTCONFIG) $(obj)/config.h $(objutil)/kconfig/conf \
	$(ediie-capsule-policy)
	+$(MAKE) -C $(EDIIE_SOURCE) coreboot-stage \
		COREBOOT_SOURCE="$(abspath .)" \
		COREBOOT_CONFIG="$(abspath $(DOTCONFIG))" \
		COREBOOT_CONFIG_HEADER="$(abspath $(obj)/config.h)" \
		COREBOOT_OUTPUT_DIR="$(abspath $(EDIIE_OUTPUT))" \
		EDIIE_KCONFIG_TOOL="$(abspath $(objutil)/kconfig/conf)" \
		HOSTCC="$(HOSTCC)" CC="$(CC_x86_64)" \
		EDIIE_NATIVE_LD="$(EDIIE_NATIVE_LD)" \
		OBJCOPY="$(OBJCOPY_x86_64)" NM="$(NM_x86_64)" \
		READELF="$(READELF_x86_64)"
