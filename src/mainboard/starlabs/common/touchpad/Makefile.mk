# SPDX-License-Identifier: GPL-2.0-only

ramstage-y += touchpad.c
smm-$(CONFIG_STARLABS_ACPI_EFI_OPTION_SMI) += touchpad.c
