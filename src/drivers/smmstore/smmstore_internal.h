/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DRIVERS_SMMSTORE_INTERNAL_H
#define DRIVERS_SMMSTORE_INTERNAL_H

#include <commonlib/bsd/cb_err.h>

#define SMMSTORE_REGION "SMMSTORE"

struct region;

enum cb_err smmstore_lookup_fmap_region(struct region *region);

#endif
