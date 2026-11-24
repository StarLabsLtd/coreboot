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
# Kconfig reads the canonical path before this makefile is included.
ifneq ($(realpath $(EDIIE_SOURCE)),$(realpath payloads/external/ediie/ediie))
$(error Point payloads/external/ediie/ediie at the prepared EDIIE_SOURCE checkout before configuring)
endif
EDIIE_OUTPUT := $(obj)/ediie
EDIIE_PAYLOAD := $(EDIIE_OUTPUT)/native/ediie-coreboot-image.elf
override CONFIG_PAYLOAD_FILE := $(EDIIE_PAYLOAD)

cbfs-files-$(CONFIG_EDIIE_SCREEN_READER) += ediie/speech
ediie/speech-file := $(EDIIE_OUTPUT)/native/spoken-drivers.bin
ediie/speech-type := raw
ediie/speech-compression := none

# coreboot-stage generates the speech bundle alongside the payload.
$(EDIIE_OUTPUT)/native/spoken-drivers.bin: $(EDIIE_PAYLOAD)

ifeq ($(CONFIG_EDIIE_SECURE_BOOT_FIXED_KEYS),y)
.PHONY: ediie-fixed-policy-check
ediie-fixed-policy-check:

$(obj)/ass/fixed_policy.h: ediie-fixed-policy-check | $(EDIIE_PAYLOAD)
	@mkdir -p "$(dir $@)"
ifneq ($(findstring n,$(filter-out --%,$(firstword $(MAKEFLAGS)))),)
	@echo "Generate edIIe fixed policy header $@"
else
	+$(MAKE) -C $(EDIIE_SOURCE) fixed-policy-header \
		COREBOOT_SOURCE="$(abspath .)" \
		COREBOOT_CONFIG="$(abspath $(DOTCONFIG))" \
		COREBOOT_CONFIG_HEADER="$(abspath $(obj)/config.h)" \
		EDIIE_BUILD_DIR="$(abspath $(EDIIE_OUTPUT))" \
		EDIIE_KCONFIG_TOOL="$(abspath $(objutil)/kconfig/conf)" \
		EDIIE_FIXED_POLICY_HEADER="$(abspath $@)" \
		HOSTCC="$(HOSTCC)" CC="$(CC_x86_64)" \
		EDIIE_NATIVE_LD="$(EDIIE_NATIVE_LD)" \
		EDIIE_NATIVE_OBJCOPY="$(OBJCOPY_x86_64)" \
		EDIIE_NATIVE_NM="$(NM_x86_64)" \
		EDIIE_NATIVE_READELF="$(READELF_x86_64)"
endif
endif

# binutils built only for ELF may lack the PE emulation used by the modules.
EDIIE_NATIVE_LD ?= $(shell command -v ld)
.PHONY: ediie-source-check
ediie-source-check:
	@sh payloads/external/ediie/verify-source.sh "$(EDIIE_SOURCE)"

$(EDIIE_PAYLOAD): ediie-source-check $(DOTCONFIG) $(obj)/config.h $(objutil)/kconfig/conf \
	$(ediie-capsule-policy)
# A dry run does not generate the configuration needed by coreboot-stage.
ifneq ($(findstring n,$(filter-out --%,$(firstword $(MAKEFLAGS)))),)
	@echo "Build edIIe payload $(EDIIE_PAYLOAD)"
else
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
ifeq ($(CONFIG_EDIIE_SCREEN_READER),y)
	@set -e; size=$$(wc -c < "$(EDIIE_OUTPUT)/native/spoken-drivers.bin"); \
		limit=$$(( $$(sed -n 's/^#define LB_EDIIE_SPEECH_MAX_BYTES //p' \
			src/commonlib/include/commonlib/coreboot_tables.h) )); \
		if [ "$$size" -eq 0 ] || [ "$$size" -gt "$$limit" ]; then \
			echo "edIIe speech bundle is $$size bytes; expected 1 to $$limit" >&2; \
			exit 1; \
		fi
endif
ifeq ($(CONFIG_STARLABS_VBOOT_RELEASE_POLICY)$(CONFIG_VBOOT_SLOTS_RW_AB),yy)
	@grep -qx 'CONFIG_EDIIE_NATIVE_SYSTEM_FMP=y' "$(EDIIE_OUTPUT)/.config" && \
		grep -qx 'CONFIG_EDIIE_NATIVE_CAPSULE_DISK_SCANNER=y' "$(EDIIE_OUTPUT)/.config" || { \
		echo 'Star Labs VBOOT A/B requires edIIe system FMP and capsule-on-disk' >&2; exit 1; }
endif
endif
