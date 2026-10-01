/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <starlabs/efi_option_smi.h>
#include <starlabs/smi.h>

int __weak variant_smi_apmc(u8 command)
{
	return 0;
}

int mainboard_smi_apmc(u8 command)
{
	if (CONFIG(STARLABS_ACPI_EFI_OPTION_SMI) && !CONFIG(PAYLOAD_MM_INTERFACE) &&
	    command == STARLABS_APMC_CMD_EFI_OPTION)
		return starlabs_efi_option_smi(command);

	return variant_smi_apmc(command);
}
