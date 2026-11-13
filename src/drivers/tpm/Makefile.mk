## SPDX-License-Identifier: GPL-2.0-only

ifeq ($(CONFIG_TPM),y)

ifeq ($(CONFIG_PAYLOAD_CDK2)$(CONFIG_DRIVERS_EMULATION_QEMU_FW_CFG),yy)
ramstage-$(CONFIG_HAVE_ACPI_TABLES) += ppi_mmio.c
endif

ramstage-$(CONFIG_TPM_INIT_RAMSTAGE) += tpm.c

ifeq ($(CONFIG_TPM_PPI),y)
ramstage-$(CONFIG_HAVE_ACPI_TABLES) += ppi.c
else
ramstage-$(CONFIG_HAVE_ACPI_TABLES) += ppi_stub.c
endif

all-$(CONFIG_TPM_GOOGLE) += cr50.c

endif
