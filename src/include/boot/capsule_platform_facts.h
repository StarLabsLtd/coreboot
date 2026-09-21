/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_CAPSULE_PLATFORM_FACTS_H
#define BOOT_CAPSULE_PLATFORM_FACTS_H

#include <boot/capsule_broker_buffers.h>
#include <boot/coreboot_tables.h>
#include <fmap.h>

#define CAPSULE_PLATFORM_FACTS_REVISION 1U

struct capsule_platform_facts {
	uint32_t revision;
	uint32_t size;
	uint64_t boot_media_size;
	uint32_t block_size;
	uint32_t erase_size;
	struct capsule_broker_buffer_reservation buffers;
	struct capsule_broker_scratch_reservation scratch;
	struct lb_efi_fw_info firmware;
	struct fmap_inventory fmap;
} __aligned(8);

_Static_assert(sizeof(struct capsule_platform_facts) == 1488,
	"capsule platform facts layout");

enum cb_err capsule_platform_facts_collect(struct capsule_platform_facts *facts);

#endif
