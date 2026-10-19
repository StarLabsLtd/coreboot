# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_STARLABS_VBOOT_RELEASE_POLICY),y)
ifeq ($(CONFIG_VBOOT),y)
starlabs-vboot-localversion := $(call strip_quotes,$(CONFIG_LOCALVERSION))
starlabs-vboot-release-major := $(shell printf '%s\n' '$(starlabs-vboot-localversion)' | \
	sed -n 's/^[^0-9]*\([0-9][0-9]*\)[.]\([0-9][0-9]*\).*/\1/p')
starlabs-vboot-release-minor := $(shell printf '%s\n' '$(starlabs-vboot-localversion)' | \
	sed -n 's/^[^0-9]*\([0-9][0-9]*\)[.]\([0-9][0-9]*\).*/\2/p')
starlabs-vboot-release-version := $(shell \
	major='$(starlabs-vboot-release-major)'; minor='$(starlabs-vboot-release-minor)'; \
	if test -n "$$major" && test "$$major" -le 255 && test "$$minor" -le 255; then \
		expr "$$major" \* 256 + "$$minor"; \
	fi)
ifeq ($(starlabs-vboot-release-version),)
$(error Star Labs VBOOT requires a major.minor CONFIG_LOCALVERSION with each component <= 255)
endif
ifeq ($(CONFIG_DRIVERS_EFI_FW_INFO),y)
ifneq ($(CONFIG_DRIVERS_EFI_MAIN_FW_VERSION),0x00000000)
$(error Star Labs VBOOT derives the EFI version from CONFIG_LOCALVERSION; explicit override forbidden)
endif
ifneq ($(CONFIG_DRIVERS_EFI_MAIN_FW_LSV),0x00000000)
$(error Star Labs VBOOT derives the EFI LSV from CONFIG_LOCALVERSION; explicit override forbidden)
endif
endif
ifneq ($(CONFIG_VBOOT_KEYBLOCK_VERSION),$(starlabs-vboot-release-version))
$(error Star Labs VBOOT version $(CONFIG_VBOOT_KEYBLOCK_VERSION) does not match CONFIG_LOCALVERSION $(CONFIG_LOCALVERSION))
endif
ifneq ($(words $(filter y,$(CONFIG_BOOTMEDIA_LOCK_CONTROLLER) \
	$(CONFIG_BOOTMEDIA_LOCK_WPRO_VBOOT_RO) \
	$(CONFIG_BOOTMEDIA_LOCK_FAILURE_FATAL) \
	$(CONFIG_SOC_INTEL_COMMON_BLOCK_FAST_SPI))),4)
$(error Star Labs VBOOT requires fail-closed Intel controller WP_RO locking)
endif
ifneq ($(words $(filter y,$(CONFIG_VBOOT_CLEAR_RECOVERY_AFTER_BOOTMEDIA_LOCKDOWN) \
	$(CONFIG_VBOOT_MARK_BOOT_SUCCESSFUL_AFTER_BOOTMEDIA_LOCKDOWN))),2)
$(error Star Labs VBOOT requires post-lockdown VBNV updates)
endif
ifeq ($(CONFIG_VBOOT_MOCK_SECDATA),y)
$(error Refusing a Star Labs VBOOT image with mocked anti-rollback state)
endif
ifeq ($(strip $(STARLABS_VBOOT_KEY_DIR)),)
$(error STARLABS_VBOOT_KEY_DIR must name the externally provisioned vboot key directory)
endif
starlabs-vboot-provisioned-key-files := $(addprefix $(STARLABS_VBOOT_KEY_DIR)/, \
	root_key.vbpubk recovery_key.vbpubk firmware_data_key.vbprivk \
	firmware.keyblock kernel_subkey.vbpubk)
starlabs-vboot-key-files := \
	$(call strip_quotes,$(CONFIG_VBOOT_ROOT_KEY)) \
	$(call strip_quotes,$(CONFIG_VBOOT_RECOVERY_KEY)) \
	$(call strip_quotes,$(CONFIG_VBOOT_FIRMWARE_PRIVKEY)) \
	$(call strip_quotes,$(CONFIG_VBOOT_KEYBLOCK)) \
	$(call strip_quotes,$(CONFIG_VBOOT_KERNEL_KEY))
ifneq ($(starlabs-vboot-key-files),$(starlabs-vboot-provisioned-key-files))
$(error Configured vboot key inputs must match STARLABS_VBOOT_KEY_DIR)
endif
starlabs-vboot-missing-key-files := \
	$(filter-out $(wildcard $(starlabs-vboot-key-files)),$(starlabs-vboot-key-files))
ifneq ($(strip $(starlabs-vboot-missing-key-files)),)
$(error Missing vboot key inputs: $(starlabs-vboot-missing-key-files))
endif
starlabs-vboot-development-key-detected := $(shell \
	for candidate in $(starlabs-vboot-key-files); do \
		if find "$(VBOOT_SOURCE)/tests" -type f -path '*/devkeys*/*' \
			-exec cmp -s "$$candidate" {} \; -print -quit | grep -q .; then \
			printf 1; exit; \
		fi; \
	done)
ifneq ($(starlabs-vboot-development-key-detected),)
ifneq ($(STARLABS_VBOOT_ALLOW_TEST_KEYS),1)
$(error Refusing in-tree vboot test keys; set STARLABS_VBOOT_ALLOW_TEST_KEYS=1 only for development builds)
endif
endif
endif
endif

CPPFLAGS_common += -I$(src)/mainboard/$(MAINBOARDDIR)/include

bootblock-$(CONFIG_BOARD_STARLABS_ADL_SERIES) += bootblock.c
bootblock-$(CONFIG_BOARD_STARLABS_STARFIGHTER_SERIES) += bootblock.c

ifneq ($(filter y,$(CONFIG_BOARD_STARLABS_LITE_SERIES) \
		$(CONFIG_BOARD_STARLABS_STARBOOK_SERIES) \
		$(CONFIG_STARLABS_VBOOT_RELEASE_POLICY)),)
verstage-$(CONFIG_VBOOT) += vboot.c
romstage-$(CONFIG_VBOOT) += vboot.c
endif

ramstage-$(CONFIG_BOARD_STARLABS_STARBOOK_SERIES) += mainboard.c
ramstage-$(CONFIG_BOARD_STARLABS_STARFIGHTER_SERIES) += mainboard.c
ramstage-$(CONFIG_VENDOR_STARLABS) += fadt.c

ifneq ($(filter y,$(CONFIG_BOARD_STARLABS_LITE_SERIES) \
		$(CONFIG_BOARD_STARLABS_LABTOP_KBL) $(CONFIG_BOARD_STARLABS_LABTOP_CML)),)
ramstage-$(CONFIG_MAINBOARD_USE_LIBGFXINIT) += gma-mainboard.ads
endif

subdirs-$(CONFIG_VENDOR_STARLABS) += cfr
subdirs-$(CONFIG_VENDOR_STARLABS) += hda
subdirs-$(CONFIG_STARLABS_TOUCHPAD) += touchpad
subdirs-$(CONFIG_VENDOR_STARLABS) += powercap
subdirs-$(CONFIG_VENDOR_STARLABS) += fsp_params
subdirs-$(CONFIG_VENDOR_STARLABS) += pin_mux
subdirs-$(CONFIG_VENDOR_STARLABS) += smbios

ramstage-$(CONFIG_STARLABS_NVME_POWER_SEQUENCE) += nvme_seq.c

CPPFLAGS_common += -I$(src)/mainboard/starlabs/common/include

ramstage-$(CONFIG_STARLABS_ACPI_EFI_OPTION_SMI) += gnvs.c
smm-$(CONFIG_STARLABS_ACPI_EFI_OPTION_SMI) += smihandler.c
