/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _DRIVERS_EFI_INFO_H_
#define _DRIVERS_EFI_INFO_H_

#include <commonlib/bsd/cb_err.h>
#include <commonlib/coreboot_tables.h>
#include <stddef.h>

enum cb_err efi_capsule_policy_build(struct lb_efi_capsule_policy *policy,
				     size_t capacity, const char *region_list);

#endif
