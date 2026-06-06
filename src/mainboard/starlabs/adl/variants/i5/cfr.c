/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/i2c_bus.h>
#include <device/i2c_simple.h>
#include <option.h>
#include <static.h>
#include <common/cfr.h>

void cfr_touchscreen_update(struct sm_object *new_obj)
{
	if (get_uint_option("accelerometer", 1) == 0)
		new_obj->sm_bool.flags |= CFR_OPTFLAG_SUPPRESS;
}
