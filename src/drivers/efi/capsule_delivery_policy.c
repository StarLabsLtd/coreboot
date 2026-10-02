/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_delivery_policy.h>
#include <boot/coreboot_tables.h>
#include <stdint.h>

_Static_assert(CONFIG_DRIVERS_EFI_CAPSULE_MAX_NONPOPULATE_SIZE > 0 &&
	CONFIG_DRIVERS_EFI_CAPSULE_MAX_NONPOPULATE_SIZE <= UINT32_MAX,
	"non-populate limit must fit the EFI capsule image size");
_Static_assert(CONFIG_DRIVERS_EFI_CAPSULE_MAX_POPULATE_SIZE >= 0 &&
	CONFIG_DRIVERS_EFI_CAPSULE_MAX_POPULATE_SIZE <= UINT32_MAX,
	"populate limit must fit the EFI capsule image size");

__weak bool platform_capsule_ram_persistent(void)
{
	return false;
}

void lb_add_capsule_delivery_policy(struct lb_header *header)
{
	uint32_t transports = 0;
	struct lb_capsule_delivery_policy *record;

	if (CONFIG(DRIVERS_EFI_CAPSULE_RAM_HANDOFF) && platform_capsule_ram_persistent())
		transports |= LB_CAPSULE_DELIVERY_RAM;
	if (CONFIG(DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT))
		transports |= LB_CAPSULE_DELIVERY_DISK;

	record = (void *)lb_new_record(header);
	*record = (struct lb_capsule_delivery_policy) {
		.tag = LB_TAG_CAPSULE_DELIVERY_POLICY,
		.size = sizeof(*record),
		.revision = LB_CAPSULE_DELIVERY_POLICY_REVISION,
		.header_size = sizeof(*record),
		.allowed_transports = transports,
		.max_nonpopulate = transports ?
			CONFIG_DRIVERS_EFI_CAPSULE_MAX_NONPOPULATE_SIZE : 0,
		.max_populate = transports ? CONFIG_DRIVERS_EFI_CAPSULE_MAX_POPULATE_SIZE : 0,
	};
}
