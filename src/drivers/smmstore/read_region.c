/* SPDX-License-Identifier: GPL-2.0-only */

#include "smmstore_internal.h"

#include <boot_device.h>
#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <console/console.h>
#include <fmap.h>
#include <fmap_config.h>
#include <smmstore.h>

#define SMMSTORE_MIN_SIZE (64 * KiB)

_Static_assert(IS_ALIGNED(FMAP_SECTION_SMMSTORE_START, SMM_BLOCK_SIZE),
	"SMMSTORE FMAP region not aligned to block size");
_Static_assert(FMAP_SECTION_SMMSTORE_SIZE >= SMMSTORE_MIN_SIZE,
	"SMMSTORE FMAP region must be at least 64 KiB");
_Static_assert(FMAP_SECTION_SMMSTORE_SIZE >= SMM_BLOCK_SIZE,
	"SMMSTORE FMAP region must fit at least one logical block");

enum cb_err smmstore_lookup_fmap_region(struct region *region)
{
	if (fmap_locate_area(SMMSTORE_REGION, region)) {
		printk(BIOS_WARNING,
		       "smm store: Unable to find SMM store FMAP region '%s'\n",
		       SMMSTORE_REGION);
		return CB_ERR;
	}

	return CB_SUCCESS;
}

int smmstore_lookup_read_region(struct region_device *rstore)
{
	struct region region;
	struct region_device read_rdev;

	if (!rstore)
		return -1;
	if (smmstore_lookup_fmap_region(&region) != CB_SUCCESS)
		return -1;
	if (boot_device_ro_subregion(&region, &read_rdev) < 0)
		return -1;
	*rstore = read_rdev;
	return 0;
}
