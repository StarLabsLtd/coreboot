/* SPDX-License-Identifier: GPL-2.0-only */

#include <stddef.h>

_Static_assert(sizeof(size_t) == 4, "Execute the actual coreboot IA32 object");

size_t platform_capsule_broker_staging_size(void);
size_t mainboard_cbmem_top_reservation_size(void);

void _start(void)
{
	unsigned int status = 0;

	if (platform_capsule_broker_staging_size() != EXPECTED_STAGING_SIZE)
		status = 1;
	if (mainboard_cbmem_top_reservation_size() != EXPECTED_TOP_RESERVATION_SIZE)
		status = 2;
	/* Freestanding Linux observer: no firmware device function is called. */
	asm volatile("int $0x80" : : "a"(1), "b"(status) : "memory");
	__builtin_unreachable();
}
