/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>

/*
 * SMMSTORE is linked into every standard stage, but writable flash is only
 * initialized and consumed in ramstage and SMM.  Keep the earlier-stage
 * reference linkable without probing QEMU's command-mode pflash before DRAM.
 */
const struct region_device *boot_device_rw(void)
{
	return NULL;
}
