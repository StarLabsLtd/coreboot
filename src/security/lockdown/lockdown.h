/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef SECURITY_LOCKDOWN_LOCKDOWN_H
#define SECURITY_LOCKDOWN_LOCKDOWN_H

#include <option.h>
#include <types.h>

static inline bool enable_smm_bios_protection(void)
{
	if (CONFIG(BOOTMEDIA_SMM_BWP_RUNTIME_OPTION))
		return get_uint_option("bios_lock", CONFIG(BOOTMEDIA_SMM_BWP));
	return CONFIG(BOOTMEDIA_SMM_BWP);
}

/* Called after the platform makes SPI status-register writes immutable. */
void boot_device_wp_status_locked(void);

#endif /* SECURITY_LOCKDOWN_LOCKDOWN_H */
