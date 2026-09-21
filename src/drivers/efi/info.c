/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <console/console.h>
#include <string.h>

void lb_efi_fw_info(struct lb_header *header)
{
	struct lb_efi_fw_info *fw_info;
	struct lb_efi_fw_info info;

	if (efi_fw_info_get(&info) != CB_SUCCESS) {
		printk(BIOS_ERR, "%s(): invalid firmware identity\n", __func__);
		return;
	}
	if (!CONFIG_DRIVERS_EFI_MAIN_FW_VERSION && info.version)
		printk(BIOS_DEBUG,
		       "EFI FW version derived from CONFIG_LOCALVERSION '%s': 0x%08x\n",
		       CONFIG_LOCALVERSION, info.version);

	fw_info = (struct lb_efi_fw_info *)lb_new_record(header);
	memcpy(fw_info, &info, sizeof(info));
}
