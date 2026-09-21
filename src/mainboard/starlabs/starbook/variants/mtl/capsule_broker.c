/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker_buffers.h>
#include <commonlib/helpers.h>

size_t platform_capsule_broker_staging_size(void)
{
	return ALIGN_UP(CONFIG_ROM_SIZE + 1 * MiB,
		CAPSULE_BROKER_BUFFER_ALIGNMENT);
}
