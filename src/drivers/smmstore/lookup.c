/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <commonlib/region.h>
#include <console/console.h>
#include <fmap.h>
#include <smmstore.h>

#define SMMSTORE_REGION "SMMSTORE"

/* Early stages only need a read-only view for UEFI-backed options. */
int smmstore_lookup_region(struct region_device *rstore)
{
	static struct region_device rdev;
	static int ret;
	static bool done;
	struct region region;

	if (!done) {
		done = true;
		ret = fmap_locate_area(SMMSTORE_REGION, &region) ||
			boot_device_ro_subregion(&region, &rdev) < 0;
		if (ret)
			printk(BIOS_WARNING, "smm store: Unable to find SMM store FMAP region '%s'\n",
			       SMMSTORE_REGION);
	}

	*rstore = rdev;
	return ret ? -1 : 0;
}
