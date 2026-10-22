/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */

#include <bootmem.h>
#include <cbmem.h>
#include <commonlib/cdk2_s4_memory.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <device/device.h>
#include <device/resource.h>
#include <smmstore.h>
#include <symbols.h>
#if CONFIG(PLATFORM_USES_FSP2_0)
#include <fsp/util.h>
#endif

static bool fixed_mailbox_initialized;

static bool overlaps_mailbox(uint64_t base, uint64_t size)
{
	return size > UINT64_MAX - base ||
		(size && base < CDK2_S4_MAILBOX_BASE + CDK2_S4_MAILBOX_SIZE &&
		 CDK2_S4_MAILBOX_BASE < base + size);
}

#if CONFIG(PLATFORM_USES_FSP2_0)
static bool mailbox_is_free_ram(void)
{
	const struct hob_header *iterator;
	const struct hob_resource *resource;
	uint64_t covered = CDK2_S4_MAILBOX_BASE, previous;

	/* FSP-M has run, but device resources need not have been enumerated yet. */
	do {
		previous = covered;
		if (fsp_hob_iterator_init(&iterator) != CB_SUCCESS)
			return false;
		while (fsp_hob_iterator_get_next_resource(&iterator, &resource) == CB_SUCCESS) {
			if (resource->length > UINT64_MAX - resource->addr)
				return false;
			if (resource->type != EFI_RESOURCE_SYSTEM_MEMORY) {
				if (overlaps_mailbox(resource->addr, resource->length))
					return false;
			} else if (resource->addr <= covered &&
				   resource->addr + resource->length > covered) {
				covered = resource->addr + resource->length;
			}
		}
	} while (covered < CDK2_S4_MAILBOX_BASE + CDK2_S4_MAILBOX_SIZE && covered > previous);
	if (covered < CDK2_S4_MAILBOX_BASE + CDK2_S4_MAILBOX_SIZE)
		return false;

#if CONFIG_UDK_VERSION >= CONFIG_UDK_2017_VERSION
	/* RAM resources alone do not describe the producer's live allocations. */
	const struct hob_header *hob = fsp_get_hob_list();
	if (hob->type != HOB_TYPE_HANDOFF || hob->length < sizeof(EFI_HOB_HANDOFF_INFO_TABLE))
		return false;
	const EFI_HOB_HANDOFF_INFO_TABLE *handoff = (const void *)hob;
	if (handoff->EfiMemoryTop < handoff->EfiMemoryBottom ||
	    overlaps_mailbox(handoff->EfiMemoryBottom,
			     handoff->EfiMemoryTop - handoff->EfiMemoryBottom))
		return false;
	for (; hob->type != HOB_TYPE_END_OF_HOB_LIST; hob = fsp_next_hob(hob)) {
		if (hob->type != HOB_TYPE_MEMORY_ALLOCATION)
			continue;
		if (hob->length < sizeof(EFI_HOB_MEMORY_ALLOCATION))
			return false;
		const EFI_HOB_MEMORY_ALLOCATION *allocation = (const void *)hob;
		if (allocation->AllocDescriptor.MemoryType != EfiConventionalMemory &&
		    overlaps_mailbox(allocation->AllocDescriptor.MemoryBaseAddress,
				     allocation->AllocDescriptor.MemoryLength))
			return false;
	}
	return true;
#else
	return false;
#endif
}
#else
struct mailbox_resources {
	uint64_t covered;
	bool conflict;
};

static void check_mailbox_resource(void *arg, struct device *dev, struct resource *resource)
{
	struct mailbox_resources *memory = arg;
	uint64_t end;

	if (resource->size > UINT64_MAX - resource->base) {
		memory->conflict = true;
		return;
	}
	end = resource->base + resource->size;
	if ((resource->flags & (IORESOURCE_RESERVE | IORESOURCE_SOFT_RESERVE)) &&
	    overlaps_mailbox(resource->base, resource->size))
		memory->conflict = true;
	if ((resource->flags & IORESOURCE_CACHEABLE) && resource->base <= memory->covered &&
	    end > memory->covered)
		memory->covered = end;
}

static bool mailbox_is_free_ram(void)
{
	struct mailbox_resources memory = { .covered = CDK2_S4_MAILBOX_BASE };
	uint64_t previous;

	do {
		previous = memory.covered;
		search_global_resources(IORESOURCE_MEM, IORESOURCE_MEM,
					check_mailbox_resource, &memory);
	} while (memory.covered < CDK2_S4_MAILBOX_BASE + CDK2_S4_MAILBOX_SIZE &&
		 memory.covered > previous && !memory.conflict);
	return !memory.conflict && memory.covered >= CDK2_S4_MAILBOX_BASE + CDK2_S4_MAILBOX_SIZE;
}
#endif

int smmstore_init_com_buffer(uintptr_t *base, size_t *size)
{
	struct smmstore_params_info info;
	uintptr_t smram;
	size_t smram_size;

	if (smmstore_get_info(&info) < 0)
		return -1;
	if (!CONFIG(CDK2_S4_LAYOUT_REQUIRED)) {
		void *buffer = cbmem_add(CBMEM_ID_SMM_COMBUFFER, info.block_size);

		if (!buffer)
			return -1;
		*base = (uintptr_t)buffer;
		*size = info.block_size;
		return 0;
	}
	smm_region(&smram, &smram_size);
	if (info.block_size != CDK2_S4_MAILBOX_SIZE || !mailbox_is_free_ram() ||
	    overlaps_mailbox(smram, smram_size) ||
	    overlaps_mailbox((uintptr_t)_program, REGION_SIZE(program)) ||
	    overlaps_mailbox((uintptr_t)_stack, REGION_SIZE(stack)) ||
	    cbmem_set_lower_bound(CDK2_S4_MEMORY_BASE + CDK2_S4_MEMORY_SIZE))
		return -1;
	fixed_mailbox_initialized = true;
	*base = CDK2_S4_MAILBOX_BASE;
	*size = CDK2_S4_MAILBOX_SIZE;
	return 0;
}

int smmstore_get_com_buffer(uintptr_t *base, size_t *size)
{
	if (!CONFIG(CDK2_S4_LAYOUT_REQUIRED)) {
		const struct cbmem_entry *entry = cbmem_entry_find(CBMEM_ID_SMM_COMBUFFER);

		if (!entry)
			return -1;
		*base = (uintptr_t)cbmem_entry_start(entry);
		*size = cbmem_entry_size(entry);
		return 0;
	}
	if (!fixed_mailbox_initialized)
		return -1;
	*base = CDK2_S4_MAILBOX_BASE;
	*size = CDK2_S4_MAILBOX_SIZE;
	return 0;
}

void smmstore_add_bootmem(void)
{
	if (!CONFIG(CDK2_S4_LAYOUT_REQUIRED))
		return;
	if (!fixed_mailbox_initialized ||
	    bootmem_add_range_from(CDK2_S4_MAILBOX_BASE, CDK2_S4_MAILBOX_SIZE,
				   BM_MEM_TABLE, BM_MEM_RAM))
		die("SMMSTORE: S4 mailbox is not owned RAM\n");
	/* Keep the loader out of the MTI bins without making them OS-reserved. */
	if (bootmem_add_range_from(CDK2_S4_BIN_BASE, CDK2_S4_BIN_SIZE,
				   BM_MEM_PAYLOAD, BM_MEM_RAM))
		die("SMMSTORE: S4 allocation bins are not free RAM\n");
}
