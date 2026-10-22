/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */
#ifndef DRIVERS_EFI_FIRMWARE_INFO_H
#define DRIVERS_EFI_FIRMWARE_INFO_H

#include <commonlib/coreboot_tables.h>
#include <stdbool.h>

bool efi_firmware_info(struct lb_efi_fw_info *info);

#endif
