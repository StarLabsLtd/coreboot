/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <bootmem.h>
#if CONFIG(BOOTMEM_ALIGNED_RESERVATION_RECEIPT)
#include "bootmem_reservation_receipt_internal.h"
#endif
#include <cbmem.h>
#include <device/device.h>
#include <device/resource.h>
#include <drivers/efi/capsules.h>
#include <symbols.h>
#include <assert.h>
#include <boot/capsule_broker_buffers.h>
#include <types.h>

static int initialized;
static int table_written;
static struct memranges bootmem;
static struct memranges bootmem_os;
#if CONFIG(BOOTMEM_ALIGNED_RESERVATIONS)
struct aligned_reservation_state {
	struct bootmem_aligned_reservation_request request;
	struct bootmem_aligned_reservation_handle handle;
	struct bootmem_aligned_reservation result;
};

static struct aligned_reservation_state aligned_reservations[
	BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS];
static size_t aligned_reservation_count;
static bool aligned_reservations_resolved;
#endif
#if CONFIG(BOOTMEM_DRAM_PROVENANCE)
static struct memranges bootmem_dram;
#endif

struct range_strings {
	enum bootmem_type tag;
	const char *str;
};

static const struct range_strings type_strings[] = {
	{ BM_MEM_RAM, "RAM" },
	{ BM_MEM_RESERVED, "RESERVED" },
	{ BM_MEM_ACPI, "ACPI" },
	{ BM_MEM_NVS, "NVS" },
	{ BM_MEM_UNUSABLE, "UNUSABLE" },
	{ BM_MEM_VENDOR_RSVD, "VENDOR RESERVED" },
	{ BM_MEM_BL31, "BL31" },
	{ BM_MEM_OPENSBI, "OPENSBI" },
	{ BM_MEM_TABLE, "CONFIGURATION TABLES" },
	{ BM_MEM_SOFT_RESERVED, "SOFT RESERVED" },
	{ BM_MEM_RAMSTAGE, "RAMSTAGE" },
	{ BM_MEM_PAYLOAD, "PAYLOAD" },
	{ BM_MEM_TAG, "TAG STORAGE" },
};

static const char *bootmem_range_string(const enum bootmem_type tag)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(type_strings); i++) {
		if (type_strings[i].tag == tag)
			return type_strings[i].str;
	}

	return "UNKNOWN!";
}

static int bootmem_is_initialized(void)
{
	return initialized;
}

static int bootmem_memory_table_written(void)
{
	return table_written;
}

#if CONFIG(BOOTMEM_ALIGNED_RESERVATIONS)
#define ALIGNED_RESERVATION_HANDLE_CHECK 0x42524d52U
#define BOOTMEM_MINIMUM_GRANULARITY 4096U

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool objects_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static bool aligned_request_valid(
	const struct bootmem_aligned_reservation_request *request)
{
	return request->revision == BOOTMEM_ALIGNED_RESERVATION_REVISION &&
		request->size == sizeof(*request) && request->bytes &&
		!(request->bytes % BOOTMEM_MINIMUM_GRANULARITY) &&
		request->alignment >= BOOTMEM_MINIMUM_GRANULARITY &&
		!(request->alignment & (request->alignment - 1U)) &&
		request->alignment <= (1ULL << 32) &&
		request->limit_exclusive &&
		request->limit_exclusive <= (1ULL << 32) &&
		request->bytes <= request->limit_exclusive &&
		(request->tag == BM_MEM_RESERVED || request->tag == BM_MEM_TABLE) &&
		!request->reserved;
}

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

int bootmem_aligned_reservations_register(
	const struct bootmem_aligned_reservation_request *requests,
	size_t request_count, struct bootmem_aligned_reservation_handle *handles)
{
	struct bootmem_aligned_reservation_request snapshots[
		BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS];
	struct bootmem_aligned_reservation_handle candidates[
		BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS];
	size_t request_bytes;
	size_t handle_bytes;

	if (!request_count ||
	    request_count > BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS)
		return -1;
	request_bytes = request_count * sizeof(*requests);
	handle_bytes = request_count * sizeof(*handles);
	if (!object_valid(handles, handle_bytes, _Alignof(*handles)))
		return -1;
	memset(handles, 0, handle_bytes);
	if (!object_valid(requests, request_bytes, _Alignof(*requests)) ||
	    objects_overlap(requests, request_bytes, handles, handle_bytes) ||
	    bootmem_is_initialized() || request_count >
		ARRAY_SIZE(aligned_reservations) - aligned_reservation_count)
		return -1;
	memcpy(snapshots, requests, request_bytes);
	for (size_t index = 0; index < request_count; index++) {
		if (!aligned_request_valid(&snapshots[index]))
			return -1;
		for (size_t existing = 0; existing < aligned_reservation_count;
		     existing++)
			if (!memcmp(&snapshots[index],
				&aligned_reservations[existing].request,
				sizeof(snapshots[index])))
				return -1;
		for (size_t prior = 0; prior < index; prior++)
			if (!memcmp(&snapshots[index], &snapshots[prior],
				sizeof(snapshots[index])))
				return -1;
		const uint32_t slot = aligned_reservation_count + index + 1U;
		candidates[index] = (struct bootmem_aligned_reservation_handle) {
			.opaque = { slot, ALIGNED_RESERVATION_HANDLE_CHECK ^ slot },
		};
	}
	if (memcmp(snapshots, requests, request_bytes) ||
	    !bytes_zero(handles, handle_bytes))
		return -1;
	for (size_t index = 0; index < request_count; index++) {
		aligned_reservations[aligned_reservation_count + index].request =
			snapshots[index];
		aligned_reservations[aligned_reservation_count + index].handle =
			candidates[index];
	}
	aligned_reservation_count += request_count;
	memcpy(handles, candidates, handle_bytes);
	return 0;
}

int bootmem_aligned_reservation_register(
	const struct bootmem_aligned_reservation_request *request,
	struct bootmem_aligned_reservation_handle *handle)
{
	return bootmem_aligned_reservations_register(request, 1, handle);
}

int bootmem_aligned_reservation_query(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_aligned_reservation *reservation)
{
	struct bootmem_aligned_reservation_handle snapshot;
	const bool result_valid = object_valid(reservation, sizeof(*reservation),
		_Alignof(*reservation));

	if (result_valid)
		memset(reservation, 0, sizeof(*reservation));
	if (!result_valid || !object_valid(handle, sizeof(*handle),
		_Alignof(*handle)) || objects_overlap(handle, sizeof(*handle),
		reservation, sizeof(*reservation)))
		return -1;
	memcpy(&snapshot, handle, sizeof(snapshot));
	if (!bootmem_is_initialized() || !aligned_reservations_resolved ||
	    !snapshot.opaque[0] ||
	    snapshot.opaque[0] > aligned_reservation_count ||
	    snapshot.opaque[1] !=
		(ALIGNED_RESERVATION_HANDLE_CHECK ^ snapshot.opaque[0]) ||
	    memcmp(&snapshot,
		&aligned_reservations[snapshot.opaque[0] - 1U].handle,
		sizeof(snapshot)) || memcmp(&snapshot, handle, sizeof(snapshot)))
		return -1;
	*reservation = aligned_reservations[snapshot.opaque[0] - 1U].result;
	return 0;
}

#if CONFIG(BOOTMEM_ALIGNED_RESERVATION_RECEIPT)
static bool range_targets_type(const struct memranges *ranges, uint64_t start,
	uint64_t size, enum bootmem_type tag);

static enum cb_err sign_resolved_reservation(
	struct bootmem_reservation_receipt_authority *signer,
	const struct bootmem_aligned_reservation_handle *handle,
	const struct bootmem_aligned_reservation *reservation,
	struct bootmem_reservation_receipt *receipt)
{
	const bool signer_valid = object_valid(signer, sizeof(*signer),
		_Alignof(*signer));
	const bool receipt_valid = object_valid(receipt, sizeof(*receipt),
		_Alignof(*receipt));
	struct bootmem_reservation_receipt_authority authority = { 0 };
	struct bootmem_reservation_receipt candidate = { 0 };
	enum cb_err status = CB_ERR;

	if (!signer_valid || !receipt_valid ||
	    objects_overlap(signer, sizeof(*signer), receipt, sizeof(*receipt)) ||
	    !bootmem_reservation_receipt_authority_claim(signer, &authority))
		goto out;
	candidate = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(candidate), .boot_kind = authority.boot_kind,
		.generation = authority.generation, .sequence = authority.sequence,
		.handle = *handle, .base = reservation->base,
		.bytes = reservation->size, .tag = reservation->tag,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	if (memcmp(&candidate.handle, &authority.handle, sizeof(candidate.handle)) ||
	    bootmem_reservation_receipt_mac(authority.secret, &candidate,
		offsetof(struct bootmem_reservation_receipt, mac), candidate.mac) !=
		CB_SUCCESS)
		goto out;
	memcpy(receipt, &candidate, sizeof(candidate));
	status = CB_SUCCESS;
out:
	if (status != CB_SUCCESS && receipt_valid)
		bootmem_reservation_receipt_scrub(receipt, sizeof(*receipt));
	if (signer_valid)
		bootmem_reservation_receipt_close(signer);
	bootmem_reservation_receipt_scrub(&authority, sizeof(authority));
	bootmem_reservation_receipt_scrub(&candidate, sizeof(candidate));
	return status;
}

static void receipt_outside_authority_spans(uintptr_t receipt_start,
	size_t receipt_size, uintptr_t signer_start, size_t signer_size,
	size_t *prefix_size, size_t *suffix_offset, size_t *suffix_size)
{
	const uintptr_t receipt_last = receipt_start + receipt_size - 1U;
	const uintptr_t signer_last = signer_start + signer_size - 1U;

	*prefix_size = 0;
	*suffix_offset = 0;
	*suffix_size = 0;
	if (receipt_start < signer_start)
		*prefix_size = MIN(receipt_last, signer_start - 1U) -
			receipt_start + 1U;
	if (signer_last < receipt_last) {
		const uintptr_t suffix_start = MAX(receipt_start, signer_last + 1U);

		*suffix_offset = suffix_start - receipt_start;
		*suffix_size = receipt_last - suffix_start + 1U;
	}
}

#if defined(BOOTMEM_RECEIPT_TEST)
void bootmem_receipt_test_outside_authority_spans(uintptr_t receipt_start,
	size_t receipt_size, uintptr_t signer_start, size_t signer_size,
	size_t spans[3])
{
	receipt_outside_authority_spans(receipt_start, receipt_size,
		signer_start, signer_size, &spans[0], &spans[1], &spans[2]);
}
#endif

static void scrub_receipt_outside_authority(
	struct bootmem_reservation_receipt *receipt,
	const struct bootmem_reservation_receipt_authority *signer)
{
	size_t prefix_size;
	size_t suffix_offset;
	size_t suffix_size;

	receipt_outside_authority_spans((uintptr_t)receipt, sizeof(*receipt),
		(uintptr_t)signer, sizeof(*signer), &prefix_size, &suffix_offset,
		&suffix_size);
	bootmem_reservation_receipt_scrub(receipt, prefix_size);
	bootmem_reservation_receipt_scrub((uint8_t *)receipt + suffix_offset,
		suffix_size);
}

enum cb_err bootmem_aligned_reservation_receipt_emit(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt *receipt)
{
	struct bootmem_aligned_reservation_handle snapshot;
	const struct aligned_reservation_state *state;
	const bool handle_valid = object_valid(handle, sizeof(*handle),
		_Alignof(*handle));
	const bool signer_valid = object_valid(signer, sizeof(*signer),
		_Alignof(*signer));
	const bool receipt_valid = object_valid(receipt, sizeof(*receipt),
		_Alignof(*receipt));

	if (!handle_valid || !signer_valid || !receipt_valid ||
	    objects_overlap(handle, sizeof(*handle), signer, sizeof(*signer)) ||
	    objects_overlap(handle, sizeof(*handle), receipt, sizeof(*receipt)) ||
	    objects_overlap(signer, sizeof(*signer), receipt, sizeof(*receipt)))
		goto fail;
	memcpy(&snapshot, handle, sizeof(snapshot));
	if (!aligned_reservations_resolved || !snapshot.opaque[0] ||
	    snapshot.opaque[0] > aligned_reservation_count ||
	    snapshot.opaque[1] !=
		(ALIGNED_RESERVATION_HANDLE_CHECK ^ snapshot.opaque[0]))
		goto fail;
	state = &aligned_reservations[snapshot.opaque[0] - 1U];
	if (memcmp(&snapshot, &state->handle, sizeof(snapshot)) ||
	    memcmp(&snapshot, handle, sizeof(snapshot)) ||
	    state->request.tag != BM_MEM_TABLE ||
	    state->result.tag != state->request.tag ||
	    state->result.size != state->request.bytes ||
	    state->result.base % state->request.alignment ||
	    state->result.base > state->request.limit_exclusive - state->result.size ||
	    !range_targets_type(&bootmem, state->result.base, state->result.size,
		BM_MEM_TABLE) ||
	    !range_targets_type(&bootmem_os, state->result.base, state->result.size,
		BM_MEM_TABLE))
		goto fail;
	return sign_resolved_reservation(signer, &snapshot, &state->result, receipt);
fail:
	if (receipt_valid) {
		if (signer_valid && objects_overlap(receipt, sizeof(*receipt),
			signer, sizeof(*signer)))
			scrub_receipt_outside_authority(receipt, signer);
		else
			bootmem_reservation_receipt_scrub(receipt, sizeof(*receipt));
	}
	if (signer_valid)
		bootmem_reservation_receipt_close(signer);
	return CB_ERR;
}
#endif

static bool range_targets_type(const struct memranges *ranges, uint64_t start,
	uint64_t size, enum bootmem_type tag)
{
	const struct range_entry *range;
	uint64_t end;

	if (!size || start > UINT64_MAX - size)
		return false;
	end = start + size;
	memranges_each_entry(range, ranges) {
		if (end <= range_entry_base(range))
			break;
		if (start >= range_entry_base(range) &&
		    end <= range_entry_end(range))
			return range_entry_tag(range) == tag;
	}
	return false;
}

static bool process_aligned_reservations(void)
{
	struct memranges candidate;
	struct memranges os_candidate;
	struct bootmem_aligned_reservation results[
		BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS] = { 0 };

	if (!aligned_reservation_count) {
		aligned_reservations_resolved = true;
		return true;
	}
	memranges_clone(&candidate, &bootmem);
	memranges_clone(&os_candidate, &bootmem_os);
	for (size_t index = 0; index < aligned_reservation_count; index++) {
		const struct bootmem_aligned_reservation_request *request =
			&aligned_reservations[index].request;
		resource_t base;
		const unsigned int shift = __builtin_ctzll(request->alignment);

		if (!memranges_steal(&candidate, request->limit_exclusive - 1U,
			request->bytes, shift, BM_MEM_RAM, &base, true) ||
		    base % request->alignment ||
		    base > request->limit_exclusive - request->bytes ||
		    !range_targets_type(&os_candidate, base, request->bytes,
			BM_MEM_RAM))
			goto fail;
		memranges_insert(&candidate, base, request->bytes, request->tag);
		memranges_insert(&os_candidate, base, request->bytes, request->tag);
		results[index] = (struct bootmem_aligned_reservation) {
			.base = base,
			.size = request->bytes,
			.tag = request->tag,
		};
	}
	memranges_teardown(&bootmem);
	memranges_teardown(&bootmem_os);
	bootmem = candidate;
	bootmem_os = os_candidate;
	for (size_t index = 0; index < aligned_reservation_count; index++)
		aligned_reservations[index].result = results[index];
	aligned_reservations_resolved = true;
	return true;

fail:
	memranges_teardown(&candidate);
	memranges_teardown(&os_candidate);
	return false;
}
#endif

#if CONFIG(BOOTMEM_DRAM_PROVENANCE)
static int domain_dram_resource(struct device *dev, struct resource *res)
{
	return dev->path.type == DEVICE_PATH_DOMAIN && res->size &&
		res->base <= UINT64_MAX - res->size;
}

#if ENV_TEST
bool bootmem_domain_dram_resource_valid_for_test(struct device *dev,
	struct resource *res)
{
	return domain_dram_resource(dev, res);
}
#endif

static bool domain_dram_range_covered(resource_t base, resource_t size)
{
	const struct range_entry *range;
	resource_t cursor = base;
	resource_t end;

	if (!size || base > UINT64_MAX - size)
		return false;
	end = base + size;
	memranges_each_entry(range, &bootmem_dram) {
		if (range_entry_end(range) <= cursor)
			continue;
		if (range_entry_base(range) > cursor)
			return false;
		if (range_entry_end(range) >= end)
			return true;
		cursor = range_entry_end(range);
	}
	return false;
}

static void validate_domain_dram_resource(void *argument, struct device *dev,
	struct resource *res)
{
	bool *valid = argument;

	if (dev->path.type != DEVICE_PATH_DOMAIN || !res->size)
		return;
	if (res->base > UINT64_MAX - res->size ||
	    !domain_dram_range_covered(res->base, res->size))
		*valid = false;
}
#endif

/* Platform hook to add bootmem areas the platform / board controls. */
void __attribute__((weak)) bootmem_platform_add_ranges(void)
{
}

/* Convert bootmem tag to LB_MEM tag */
static uint32_t bootmem_to_lb_tag(const enum bootmem_type tag)
{
	switch (tag) {
	case BM_MEM_RAM:
		return LB_MEM_RAM;
	case BM_MEM_RESERVED:
		return LB_MEM_RESERVED;
	case BM_MEM_ACPI:
		return LB_MEM_ACPI;
	case BM_MEM_NVS:
		return LB_MEM_NVS;
	case BM_MEM_UNUSABLE:
		return LB_MEM_UNUSABLE;
	case BM_MEM_VENDOR_RSVD:
		return LB_MEM_VENDOR_RSVD;
	case BM_MEM_OPENSBI:
		return LB_MEM_RESERVED;
	case BM_MEM_BL31:
		return LB_MEM_RESERVED;
	case BM_MEM_TABLE:
		return LB_MEM_TABLE;
	case BM_MEM_SOFT_RESERVED:
		return LB_MEM_SOFT_RESERVED;
	case BM_MEM_TAG:
		return LB_MEM_TAG;
	default:
		printk(BIOS_ERR, "Unsupported tag %u\n", tag);
		return LB_MEM_RESERVED;
	}
}

static void bootmem_init(void)
{
	const unsigned long cacheable = IORESOURCE_CACHEABLE;
	const unsigned long reserved = IORESOURCE_RESERVE;
	const unsigned long soft_reserved = IORESOURCE_SOFT_RESERVE;
	struct memranges *bm = &bootmem;

	initialized = 1;

#if CONFIG(BOOTMEM_DRAM_PROVENANCE)
	bool dram_provenance_valid = true;

	/* Keep source provenance separate from all later ownership overlays. */
	memranges_init_empty_with_alignment(&bootmem_dram, NULL, 0, 0);
	memranges_add_resources_filter(&bootmem_dram, cacheable, cacheable,
		BM_MEM_RAM, domain_dram_resource);
	search_global_resources(cacheable | IORESOURCE_MEM,
		cacheable | IORESOURCE_MEM, validate_domain_dram_resource,
		&dram_provenance_valid);
	if (!dram_provenance_valid)
		die("Could not retain authoritative DRAM provenance\n");
#endif

	/*
	 * Fill the memory map out. The order of operations is important in
	 * that each overlapping range will take over the next. Therefore,
	 * add cacheable resources as RAM then add the reserved resources.
	 */
	memranges_init(bm, cacheable, cacheable, BM_MEM_RAM);
	memranges_add_resources(bm, reserved, reserved, BM_MEM_RESERVED);
	memranges_add_resources(bm, soft_reserved, soft_reserved, BM_MEM_SOFT_RESERVED);
	memranges_clone(&bootmem_os, bm);

	/* Add memory used by CBMEM. */
	cbmem_add_bootmem();

	efi_add_capsules_to_bootmem();

	bootmem_add_range((uintptr_t)_stack, REGION_SIZE(stack),
			  BM_MEM_RAMSTAGE);
	bootmem_add_range((uintptr_t)_program, REGION_SIZE(program),
			  BM_MEM_RAMSTAGE);

	bootmem_arch_add_ranges();
	bootmem_platform_add_ranges();
	if (CONFIG(CAPSULE_BROKER_FIXED_BUFFERS) &&
	    !capsule_broker_buffers_reserve())
		die("Capsule broker buffer reservation failed\n");
#if CONFIG(BOOTMEM_ALIGNED_RESERVATIONS)
	if (!process_aligned_reservations())
		die("Could not satisfy aligned bootmem reservations\n");
#endif
}

void bootmem_add_range(uint64_t start, uint64_t size,
		       const enum bootmem_type tag)
{
	assert(tag > BM_MEM_FIRST && tag < BM_MEM_LAST);
	assert(bootmem_is_initialized());

	memranges_insert(&bootmem, start, size, tag);
	if (tag <= BM_MEM_OS_CUTOFF) {
		/* Can't change OS tables anymore after they are written out. */
		assert(!bootmem_memory_table_written());
		memranges_insert(&bootmem_os, start, size, tag);
	};
}

int bootmem_add_range_from(uint64_t start, uint64_t size, const enum bootmem_type new_tag,
			   const enum bootmem_type from_tag)
{
	assert(new_tag != from_tag);

	if (!bootmem_region_targets_type(start, size, from_tag)) {
		printk(BIOS_ERR, "%s: Failed to add the range [%#llx, %#llx)"
		       " from tag %s to %s\n", __func__, start, start + size,
		       bootmem_range_string(from_tag), bootmem_range_string(new_tag));
		return -1;
	}

	bootmem_add_range(start, size, new_tag);

	return 0;
}

void bootmem_write_memory_table(struct lb_memory *mem)
{
	const struct range_entry *r;
	struct lb_memory_range *lb_r;

	lb_r = &mem->map[0];

	bootmem_init();
	bootmem_dump_ranges();

	memranges_each_entry(r, &bootmem_os) {
		lb_r->start = range_entry_base(r);
		lb_r->size = range_entry_size(r);
		lb_r->type = bootmem_to_lb_tag(range_entry_tag(r));

		lb_r++;
		mem->size += sizeof(struct lb_memory_range);
	}

	table_written = 1;
}

void bootmem_dump_ranges(void)
{
	int i;
	const struct range_entry *r;

	i = 0;
	memranges_each_entry(r, &bootmem) {
		printk(BIOS_DEBUG, "%2d. %016llx-%016llx: %s\n",
			i, range_entry_base(r), range_entry_end(r) - 1,
			bootmem_range_string(range_entry_tag(r)));
		i++;
	}
}

bool bootmem_walk_os_mem(range_action_t action, void *arg)
{
	const struct range_entry *r;

	assert(bootmem_is_initialized());

	memranges_each_entry(r, &bootmem_os) {
		if (!action(r, arg))
			return true;
	}

	return false;
}

bool bootmem_walk(range_action_t action, void *arg)
{
	const struct range_entry *r;

	assert(bootmem_is_initialized());

	memranges_each_entry(r, &bootmem) {
		if (!action(r, arg))
			return true;
	}

	return false;
}

#if CONFIG(BOOTMEM_DRAM_PROVENANCE)
bool bootmem_walk_dram(range_action_t action, void *arg)
{
	const struct range_entry *dram;
	const struct range_entry *final;

	assert(bootmem_is_initialized());

	memranges_each_entry(dram, &bootmem_dram) {
		memranges_each_entry(final, &bootmem) {
			struct range_entry intersection;
			resource_t base;
			resource_t end;

			if (range_entry_end(final) <= range_entry_base(dram))
				continue;
			if (range_entry_base(final) >= range_entry_end(dram))
				break;
			base = MAX(range_entry_base(dram), range_entry_base(final));
			end = MIN(range_entry_end(dram), range_entry_end(final));
			range_entry_init(&intersection, base, end,
				range_entry_tag(final));
			if (!action(&intersection, arg))
				return true;
		}
	}

	return false;
}
#endif

int bootmem_region_targets_type(uint64_t start, uint64_t size,
				enum bootmem_type dest_type)
{
	const struct range_entry *r;
	uint64_t end = start + size;

	memranges_each_entry(r, &bootmem) {
		/* All further bootmem entries are beyond this range. */
		if (end <= range_entry_base(r))
			break;

		if (start >= range_entry_base(r) && end <= range_entry_end(r)) {
			if (range_entry_tag(r) == dest_type)
				return 1;
		}
	}
	return 0;
}

void *bootmem_allocate_buffer(size_t size)
{
	const struct range_entry *r;
	const struct range_entry *region;
	/* All allocated buffers fall below the 32-bit boundary. */
	const resource_t max_addr = 1ULL << 32;
	resource_t begin;
	resource_t end;

	if (!bootmem_is_initialized()) {
		printk(BIOS_ERR, "%s: lib uninitialized!\n", __func__);
		return NULL;
	}

	/* 4KiB alignment. */
	size = ALIGN_UP(size, 4096);
	region = NULL;
	memranges_each_entry(r, &bootmem) {
		if (range_entry_base(r) >= max_addr)
			break;

		if (range_entry_size(r) < size)
			continue;

		if (range_entry_tag(r) != BM_MEM_RAM)
			continue;

		end = range_entry_end(r);
		if (end > max_addr)
			end = max_addr;

		if ((end - range_entry_base(r)) < size)
			continue;

		region = r;
	}

	if (region == NULL)
		return NULL;

	/* region now points to the highest usable region for the given size. */
	end = range_entry_end(region);
	if (end > max_addr)
		end = max_addr;
	begin = end - size;

	/* Mark buffer as unusable for future buffer use. */
	bootmem_add_range(begin, size, BM_MEM_PAYLOAD);

	return (void *)(uintptr_t)begin;
}
