/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_COMMON_MEMORY_H
#define MAINBOARD_STARLABS_COMMON_MEMORY_H

#include <option.h>

#define STARLABS_MEMORY_SPEED_MAX 2

static inline unsigned int starlabs_get_memory_speed_option(unsigned int default_speed)
{
	return get_uint_option_checked("memory_speed", default_speed,
				       OPTION_RANGE(0, STARLABS_MEMORY_SPEED_MAX));
}

#endif /* MAINBOARD_STARLABS_COMMON_MEMORY_H */
