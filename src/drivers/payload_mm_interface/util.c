/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/region.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <fmap.h>
#include <payload_mm_interface.h>
#include <smmstore.h>
#include <types.h>

void payload_mm_get_reserved_region(uintptr_t *tseg_base, size_t *tseg_size)
{
	smm_subregion(SMM_SUBREGION_PAYLOAD, tseg_base, tseg_size);
}

#define SMMSTORE_REGION "SMMSTORE"

/* FIXME: Considering the MM case: Create compile-time conflict with SMMSTORE? */
__weak int smmstore_lookup_region(struct region_device *rstore)
{
	static int done;
	static struct region_device rdev;
	static int ret;

	if (!done) {
		done = 1;

		if (fmap_locate_area_as_rdev_rw(SMMSTORE_REGION, &rdev)) {
			printk(BIOS_WARNING,
			       "smm store: Unable to find SMM store FMAP region '%s'\n",
				SMMSTORE_REGION);
			ret = -1;
		} else {
			ret = 0;
		}
	}

	*rstore = rdev;
	return ret;
}
