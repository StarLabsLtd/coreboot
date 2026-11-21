/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_owner.h>
#include <boot/payload_mm_authvar_service.h>
#include <commonlib/bsd/fmap_serialized.h>
#include <commonlib/endian.h>
#include <commonlib/helpers.h>
#include <string.h>

#include "native_capsule.h"
#include "native_capsule_source.h"

struct capsule_geometry_reply {
	uint32_t flash_size;
	uint32_t block_size;
	uint32_t floor;
} __packed;

enum {
	CAPSULE_REVISION_WITH_RESULT = 2,
};

#define CAPSULE_LAYOUT_MISMATCH (PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 25U)

bool ass_capsule_wire_payload(void *message, size_t size,
	struct ass_capsule_wire_request *request, uint8_t **payload, size_t *payload_size)
{
	const uint8_t *bytes = message;

	if (!message || size < sizeof(*request))
		return false;
	request->revision = read_le32(bytes);
	request->command = read_le32(bytes + 4);
	request->transaction = read_le32(bytes + 8);
	if (request->revision != 1 && request->revision != CAPSULE_REVISION_WITH_RESULT)
		return false;
	const size_t header = sizeof(*request) +
		(request->revision == CAPSULE_REVISION_WITH_RESULT ? sizeof(uint32_t) : 0);
	if (size < header)
		return false;
	*payload = (uint8_t *)message + header;
	*payload_size = size - header;
	return true;
}

struct capsule_signed_header {
	uint8_t signature[4];
	uint8_t header_size[4];
	uint8_t version[4];
	uint8_t floor[4];
	uint8_t image_guid[16];
};

struct capsule_manifest_footer {
	uint8_t signature[4];
	uint8_t revision[2];
	uint8_t count[2];
};

static uint64_t check_fmap_range(struct ass_capsule_source *source,
	const struct ass_capsule_geometry *geometry, uint32_t fmap_offset,
	uint32_t fmap_size, const char *name, uint32_t offset, uint32_t size)
{
	struct fmap map;
	const size_t prefix = sizeof(struct capsule_signed_header);
	bool found = false;
	bool matches = false;

	if (!source || !geometry || !name || !size ||
	    offset > geometry->flash_size || size > geometry->flash_size - offset ||
	    fmap_offset > geometry->flash_size ||
	    fmap_size > geometry->flash_size - fmap_offset || fmap_size < sizeof(map))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (prefix > source->size || geometry->flash_size > source->size - prefix ||
	    !ass_capsule_source_read(source, prefix + fmap_offset, &map, sizeof(map)))
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	const uint16_t count = read_le16(&map.nareas);
	if (memcmp(map.signature, FMAP_SIGNATURE, sizeof(map.signature)) ||
	    map.ver_major != FMAP_VER_MAJOR || map.ver_minor > FMAP_VER_MINOR ||
	    read_le32(&map.size) != geometry->flash_size ||
	    count > (fmap_size - sizeof(map)) / sizeof(struct fmap_area))
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;

	for (size_t i = 0; i < count; i++) {
		struct fmap_area area;

		if (!ass_capsule_source_read(source, prefix + fmap_offset + sizeof(map) +
		    i * sizeof(area), &area, sizeof(area)))
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		if (strncmp((const char *)area.name, name, sizeof(area.name)))
			continue;
		const uint32_t start = read_le32(&area.offset);
		const uint32_t length = read_le32(&area.size);
		if (found || !length || start >= geometry->flash_size ||
		    length > geometry->flash_size - start)
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		found = true;
		matches = start == offset && length == size;
	}
	if (!found)
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	return matches ? PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS : CAPSULE_LAYOUT_MISMATCH;
}

uint64_t ass_native_capsule_manifest(struct ass_capsule_source *source,
	const struct ass_capsule_geometry *geometry, const char (*allowlist)[16],
	size_t allowed, uint32_t *selected)
{
	struct capsule_manifest_footer footer;
	uint32_t mask = 0;
	const size_t prefix = sizeof(struct capsule_signed_header);

	if (!source || !geometry || !allowlist || !allowed ||
	    allowed > ASS_CAPSULE_MAX_RANGES || !selected)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (!source->authorized || source->size < prefix + sizeof(uint64_t) ||
	    geometry->flash_size > source->size - prefix - sizeof(uint64_t))
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	const size_t manifest_size = source->size - prefix - sizeof(uint64_t) -
		geometry->flash_size;
	if (!manifest_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
	if (manifest_size < sizeof(footer))
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	if (!ass_capsule_source_read(source, source->size - sizeof(uint64_t) - sizeof(footer),
	    &footer, sizeof(footer)))
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	const size_t entries = read_le16(footer.count);
	if (memcmp(footer.signature, "RMAP", sizeof(footer.signature)) ||
	    read_le16(footer.revision) != 1 || !entries || entries > allowed ||
	    entries * sizeof(*allowlist) != manifest_size - sizeof(footer))
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	for (size_t i = 0; i < entries; i++) {
		char name[16];
		size_t index;

		if (!ass_capsule_source_read(source,
		    prefix + geometry->flash_size + i * sizeof(name), name, sizeof(name)))
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		for (index = 0; index < allowed; index++)
			if (!memcmp(name, allowlist[index], sizeof(name)))
				break;
		if (index == allowed || (mask & (1U << index)))
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		mask |= 1U << index;
	}
	*selected = mask;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint64_t ass_native_capsule_select_ranges(struct ass_capsule_source *source,
	const struct ass_capsule_geometry *geometry, struct ass_capsule_range fmap,
	struct ass_capsule_range store, const char (*names)[16],
	const struct ass_capsule_range *ranges, size_t count, uint32_t *selected)
{
	uint32_t mask;
	bool layout_changed = false;

	if (!ranges || !selected)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	uint64_t status = ass_native_capsule_manifest(source, geometry, names, count, &mask);
	if (status)
		return status;
	status = check_fmap_range(source, geometry, fmap.offset, fmap.size,
		"SMMSTORE", store.offset, store.size);
	if (status == CAPSULE_LAYOUT_MISMATCH)
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	if (status)
		return status;

	for (size_t i = 0; i < count; i++) {
		if (!(mask & (1U << i)))
			continue;
		char name[17];
		memcpy(name, names[i], sizeof(names[i]));
		name[sizeof(names[i])] = '\0';
		status = check_fmap_range(source, geometry, fmap.offset, fmap.size,
			name, ranges[i].offset, ranges[i].size);
		if (status == CAPSULE_LAYOUT_MISMATCH)
			layout_changed = true;
		else if (status)
			return status;
		if (ranges[i].offset < (uint64_t)store.offset + store.size &&
		    store.offset < (uint64_t)ranges[i].offset + ranges[i].size)
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		for (size_t j = 0; j < i; j++) {
			if ((mask & (1U << j)) &&
			    ranges[i].offset < (uint64_t)ranges[j].offset + ranges[j].size &&
			    ranges[j].offset < (uint64_t)ranges[i].offset + ranges[i].size)
				return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		}
	}
	if (layout_changed)
		return CAPSULE_LAYOUT_MISMATCH;
	*selected = mask;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint64_t ass_native_capsule_target(struct ass_capsule_source *source,
	const struct ass_capsule_geometry *geometry, uint32_t image_offset, uint32_t floor)
{
	struct capsule_signed_header header;

	if (!source || !geometry || !geometry->flash_size ||
	    source->size < geometry->flash_size ||
	    source->size - geometry->flash_size < sizeof(header) + sizeof(uint64_t) ||
	    source->size - geometry->flash_size > ASS_CAPSULE_SOURCE_BLOCK ||
	    !ass_capsule_source_read(source, 0, &header, sizeof(header)))
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	if (memcmp(header.signature, "MSS1", sizeof(header.signature)) ||
	    read_le32(header.header_size) != sizeof(header) || image_offset != sizeof(header))
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	if (memcmp(header.image_guid, geometry->image_guid, sizeof(header.image_guid)))
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	const uint32_t version = read_le32(header.version);
	if (version < MAX(floor, geometry->compiled_floor) || read_le32(header.floor) > version)
		return PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 25U; /* EFI_INCOMPATIBLE_VERSION */
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static uint64_t process_block(const struct ass_capsule_plan *plan,
	struct ass_capsule_source *source, uint32_t block, uint8_t *image,
	ass_capsule_program_fn *program, bool preflight)
{
	const size_t offset = (size_t)block * ASS_CAPSULE_SOURCE_BLOCK;

	for (size_t i = 0; i < plan->count; i++) {
		const struct efi_capsule_range *range = &plan->ranges[i];

		if (offset >= range->offset + range->size ||
		    range->offset >= offset + ASS_CAPSULE_SOURCE_BLOCK)
			continue;
		if (!ass_capsule_source_read(source, sizeof(struct capsule_signed_header) + offset,
		    image, ASS_CAPSULE_SOURCE_BLOCK))
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		return program(offset, plan->ranges, plan->count, image, preflight) ?
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR : PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static uint64_t check_bios_range(struct ass_capsule_source *source, uint32_t flash_size,
	struct ass_capsule_range bios)
{
	const size_t prefix = sizeof(struct capsule_signed_header);
	uint8_t descriptor[8], region[4];

	if (!bios.size || bios.offset >= flash_size || bios.size > flash_size - bios.offset)
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	if (!ass_capsule_source_read(source, prefix + 0x10, descriptor, sizeof(descriptor)))
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	if (read_le32(descriptor) != 0x0ff0a55a)
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;

	/* FLMAP0 points to a 16-byte-aligned region table. BIOS is entry one. */
	const size_t offset = ((read_le32(descriptor + 4) >> 16) & 0xff) * 16 + 4;
	if (offset < 0x18 || offset > flash_size || sizeof(region) > flash_size - offset ||
	    !ass_capsule_source_read(source, prefix + offset, region, sizeof(region)))
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	const uint32_t flreg = read_le32(region);
	const uint32_t start = (flreg & 0x7fff) << 12;
	const uint32_t end = (((flreg >> 16) & 0x7fff) + 1) << 12;
	if (start != bios.offset || end <= start || end - start != bios.size)
		return PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 25U; /* EFI_INCOMPATIBLE_VERSION */
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static uint64_t select_inactive_slot(struct ass_capsule_plan *candidate,
	struct ass_capsule_source *source, const struct ass_capsule_geometry *geometry,
	const char (*names)[16], const struct ass_capsule_range *ranges, size_t count,
	uint32_t *mask, uint32_t boot_slot)
{
	static const char slot_names[][16] = {
		"FW_MAIN_A", "RW_FWID_A", "VBLOCK_A", "FW_MAIN_B", "RW_FWID_B", "VBLOCK_B",
	};
	const size_t manifest = sizeof(struct capsule_signed_header) + geometry->flash_size;
	uint32_t exact = 0;
	struct efi_capsule_range active[3];

	for (size_t i = 0; i < count; i++)
		if (*mask == (1U << i) && !strcmp(names[i], "EC"))
			return 0;
	for (size_t i = 0; i < ARRAY_SIZE(slot_names); i++) {
		char name[16];
		size_t j;

		if (!ass_capsule_source_read(source, manifest + i * sizeof(name), name,
		    sizeof(name)) || memcmp(name, slot_names[i], sizeof(name)))
			return CAPSULE_LAYOUT_MISMATCH;
		for (j = 0; j < count; j++)
			if ((*mask & (1U << j)) && !memcmp(names[j], name, sizeof(name)))
				break;
		if (j == count)
			return CAPSULE_LAYOUT_MISMATCH;
		exact |= 1U << j;
		const uint32_t target = boot_slot == 1 ? 2 : 1;
		if ((i / 3) == target - 1)
			candidate->ranges[candidate->count++] = (struct efi_capsule_range) {
				.offset = ranges[j].offset, .size = ranges[j].size,
			};
		else if (boot_slot != 3)
			active[i % 3] = (struct efi_capsule_range) {
				.offset = ranges[j].offset, .size = ranges[j].size,
			};
	}
	if (exact != *mask)
		return CAPSULE_LAYOUT_MISMATCH;
	for (size_t i = 0; i < candidate->count; i++) {
		const struct efi_capsule_range *range = &candidate->ranges[i];
		const uint64_t start = ALIGN_DOWN((uint64_t)range->offset, geometry->block_size);
		const uint64_t end = ALIGN_UP((uint64_t)range->offset + range->size,
			geometry->block_size);

		for (size_t j = 0; boot_slot != 3 && j < ARRAY_SIZE(active); j++)
			if (start < (uint64_t)active[j].offset + active[j].size &&
			    active[j].offset < end)
				return CAPSULE_LAYOUT_MISMATCH;
	}
	candidate->trial_slot = boot_slot == 1 ? 2 : 1;
	*mask = 0;
	return 0;
}

uint64_t ass_native_capsule_begin(struct ass_capsule_plan *plan,
	struct ass_capsule_source *source,
	const struct ass_capsule_geometry *geometry, struct ass_capsule_range fmap,
	struct ass_capsule_range store, struct ass_capsule_range bios, const char (*names)[16],
	const struct ass_capsule_range *ranges, size_t count, uint32_t floor,
	uint8_t *image, ass_capsule_program_fn *program, uint32_t boot_slot)
{
	const size_t prefix = sizeof(struct capsule_signed_header);
	struct ass_capsule_plan candidate = { 0 };
	uint32_t mask;

	if (!plan || !geometry || !image || !program || boot_slot > 3 ||
	    geometry->block_size != ASS_CAPSULE_SOURCE_BLOCK ||
	    geometry->flash_size % geometry->block_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	uint64_t status = ass_native_capsule_target(source, geometry, prefix, floor);
	if (status)
		return status;
	status = ass_native_capsule_select_ranges(source, geometry, fmap, store, names,
		ranges, count, &mask);
	if (boot_slot) {
		if (status)
			return status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND ?
				CAPSULE_LAYOUT_MISMATCH : status;
		status = select_inactive_slot(&candidate, source, geometry, names, ranges,
			count, &mask, boot_slot);
		if (status)
			return status;
	}
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND || status == CAPSULE_LAYOUT_MISMATCH) {
		status = check_fmap_range(source, geometry, fmap.offset, fmap.size,
			"SMMSTORE", store.offset, store.size);
		if (!status)
			status = check_bios_range(source, geometry->flash_size, bios);
		if (status)
			return status;
		candidate.ranges[candidate.count++] = (struct efi_capsule_range) {
			.offset = bios.offset, .size = bios.size,
		};
		mask = 0;
	} else if (status) {
		return status;
	}
	for (size_t i = 0; i < count; i++)
		if (mask & (1U << i))
			candidate.ranges[candidate.count++] = (struct efi_capsule_range) {
				.offset = ranges[i].offset, .size = ranges[i].size,
			};

	candidate.blocks = geometry->flash_size / geometry->block_size;
	for (uint32_t block = 0; block < candidate.blocks; block++) {
		status = process_block(&candidate, source, block, image, program, true);
		if (status)
			return status;
	}
	*plan = candidate;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint64_t ass_native_capsule_program(struct ass_capsule_plan *plan,
	struct ass_capsule_source *source, uint32_t block, uint8_t *image,
	ass_capsule_program_fn *program)
{
	if (!plan || !source || !source->authorized || !image || !program ||
	    block != plan->next || block >= plan->blocks)
		return PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
	uint64_t status = process_block(plan, source, block, image, program, false);
	if (status) {
		plan->blocks = 0;
		ass_capsule_source_close(source);
	} else {
		plan->next++;
	}
	return status;
}

uint32_t ass_native_capsule_activate(struct ass_capsule_transaction *transaction)
{
	if (transaction->active || !transaction->plan.blocks)
		return 0;
	if (++transaction->last == 0)
		transaction->last++;
	transaction->active = transaction->last;
	return transaction->active;
}

void ass_native_capsule_close(struct ass_capsule_transaction *transaction,
	struct ass_capsule_source *source)
{
	ass_capsule_source_close(source);
	transaction->plan.blocks = 0;
	transaction->active = 0;
}

static uint32_t capsule_result(void *message, uint32_t revision, uint64_t status)
{
	const uint32_t result = (uint32_t)status | ((status >> 32) & (1U << 31));

	if (revision != CAPSULE_REVISION_WITH_RESULT)
		return result;
	write_le32((uint8_t *)message + sizeof(struct ass_capsule_wire_request), result);
	return 0;
}

uint32_t ass_native_capsule_continue(struct ass_capsule_transaction *transaction,
	struct ass_capsule_source *source, void *message, size_t size, uint8_t *image,
	ass_capsule_program_fn *program, uint32_t (*commit)(uint32_t slot))
{
	struct ass_capsule_wire_request request;
	uint8_t *payload;
	size_t payload_size;
	uint64_t status;

	if (!transaction || !source ||
	    !ass_capsule_wire_payload(message, size, &request, &payload, &payload_size))
		return 0x80000002U;
	if (!request.transaction || request.transaction != transaction->active) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
	} else if (request.command == ASS_CAPSULE_CLOSE && !payload_size) {
		ass_native_capsule_close(transaction, source);
		status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	} else if (request.command == ASS_CAPSULE_PROGRAM && payload_size == sizeof(uint32_t)) {
		struct ass_capsule_plan *plan = &transaction->plan;

		if (plan->trial_slot && !commit) {
			ass_native_capsule_close(transaction, source);
			status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		} else {
			status = ass_native_capsule_program(plan, source, read_le32(payload),
				image, program);
			if (!status && plan->trial_slot && plan->next == plan->blocks &&
			    commit(plan->trial_slot)) {
				ass_native_capsule_close(transaction, source);
				status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
			}
		}
	} else {
		status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	}
	return capsule_result(message, request.revision, status);
}

uint64_t ass_native_capsule_floor(struct authvar_owner *owner,
	const uint8_t image_guid[16], uint32_t compiled_floor, uint32_t *floor)
{
	const uint16_t state_name[] = { 'F', 'm', 'p', 'S', 't', 'a', 't', 'e', 0 };
	const uint16_t lsv_name[] = { 'F', 'm', 'p', 'L', 's', 'v', 0 };
	/* FMP controller state persists as four validity bytes and four LE32 values. */
	uint8_t state[20];
	size_t size = sizeof(state);
	uint32_t stored_floor = 0;
	uint32_t attributes = 0;

	if (!owner || !image_guid || !floor)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (owner->runtime)
		return PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
	uint64_t status = authvar_owner_get(owner, state_name, image_guid, &attributes, &size, state);
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		if (!(attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE) ||
		    size != sizeof(state) || state[0] > 1 || state[1] > 1 ||
		    state[2] > 1 || state[3] > 1)
			return PAYLOAD_MM_AUTHVAR_STATUS_VOLUME_CORRUPTED;
		if (state[1])
			stored_floor = read_le32(state + 8);
	} else if (status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND) {
		size = sizeof(uint32_t);
		status = authvar_owner_get(owner, lsv_name, image_guid, &attributes, &size, state);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			if (!(attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE) ||
			    size != sizeof(uint32_t))
				return PAYLOAD_MM_AUTHVAR_STATUS_VOLUME_CORRUPTED;
			stored_floor = read_le32(state);
		} else if (status != PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND) {
			return status;
		}
	} else {
		return status;
	}
	*floor = MAX(compiled_floor, stored_floor);
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint32_t ass_native_capsule_info(struct authvar_owner *owner,
	const struct ass_capsule_geometry *geometry, void *message, size_t size)
{
	struct ass_capsule_wire_request request;
	uint8_t *payload;
	size_t payload_size;
	uint64_t status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	uint32_t floor;

	if (!owner || !geometry || !message || size < sizeof(request))
		return 0x80000002U;
	const uint32_t revision = read_le32(message);
	if (revision != 1 && revision != CAPSULE_REVISION_WITH_RESULT)
		return 0x80000003U;
	if (!ass_capsule_wire_payload(message, size, &request, &payload, &payload_size))
		return 0x80000002U;
	if (request.command != ASS_CAPSULE_INFO) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	} else if (!request.transaction && payload_size == sizeof(struct capsule_geometry_reply) &&
		   geometry->block_size && geometry->flash_size &&
		   !(geometry->flash_size % geometry->block_size)) {
		status = ass_native_capsule_floor(owner, geometry->image_guid,
			geometry->compiled_floor, &floor);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			write_le32(payload, geometry->flash_size);
			write_le32(payload + 4, geometry->block_size);
			write_le32(payload + 8, floor);
		}
	}
	return capsule_result(message, revision, status);
}

bool ass_capsule_source_in_ram(uint64_t address, size_t size, uintptr_t ram_start,
	uintptr_t ram_end, uintptr_t mailbox, size_t mailbox_size)
{
	if (!ram_start || !size || address < ram_start || address >= ram_end ||
	    size > ram_end - address || !mailbox || !mailbox_size ||
	    mailbox_size > UINTPTR_MAX - mailbox)
		return false;
	return address + size <= mailbox || address >= mailbox + mailbox_size;
}

uint32_t ass_native_capsule_dispatch(struct ass_capsule_transaction *transaction,
	struct ass_capsule_source *source, struct authvar_owner *owner,
	struct payload_mm_crypto_owner *crypto, const struct ass_capsule_platform *platform,
	void *message, size_t size, uint8_t *image)
{
	struct ass_capsule_wire_request request;
	uint8_t *payload;
	size_t payload_size;
	uint32_t floor;

	if (!transaction || !source || !owner || !platform ||
	    !ass_capsule_wire_payload(message, size, &request, &payload, &payload_size))
		return 0x80000002U;
	if (request.command == ASS_CAPSULE_INFO || request.command == ASS_CAPSULE_BEGIN) {
		const uint64_t status = ass_native_capsule_floor(owner,
			platform->geometry.image_guid, platform->geometry.compiled_floor, &floor);
		if (status)
			return capsule_result(message, request.revision, status);
		transaction->floor = MAX(transaction->floor, floor);
	}
	if (request.command == ASS_CAPSULE_INFO) {
		struct ass_capsule_geometry geometry = platform->geometry;

		geometry.compiled_floor = transaction->floor;
		return ass_native_capsule_info(owner, &geometry, message, size);
	}
	if (request.command == ASS_CAPSULE_CLOSE)
		return ass_native_capsule_continue(transaction, source, message, size,
			image, platform->program, platform->commit);
	if (owner->runtime)
		return capsule_result(message, request.revision, PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED);
	if (request.command == ASS_CAPSULE_PROGRAM)
		return ass_native_capsule_continue(transaction, source, message, size,
			image, platform->program, platform->commit);
	if (request.command != ASS_CAPSULE_BEGIN)
		return capsule_result(message, request.revision, PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	if (transaction->active || request.transaction)
		return capsule_result(message, request.revision, PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED);
	if (platform->boot_slot && !platform->commit)
		return capsule_result(message, request.revision, PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	if (payload_size <= 20 || read_le32(payload + 16) != payload_size - 20 ||
	    read_le32(payload + 12) != sizeof(struct capsule_signed_header) ||
	    !platform->map_source)
		return capsule_result(message, request.revision, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);

	const uint32_t signed_size = read_le32(payload + 8);
	if (signed_size <= platform->geometry.flash_size ||
	    signed_size - platform->geometry.flash_size > ASS_CAPSULE_SOURCE_BLOCK)
		return capsule_result(message, request.revision, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	const void *external = platform->map_source(read_le64(payload), signed_size);
	if (!external)
		return capsule_result(message, request.revision, PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED);
	const struct payload_mm_crypto_span cms = {
		.data = payload + 20, .size = payload_size - 20,
	};
	if (ass_capsule_source_verify(source, crypto, external, signed_size, &cms, platform->root)
	    != PAYLOAD_MM_VERIFY_OK)
		return capsule_result(message, request.revision, PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	uint64_t status = ass_native_capsule_begin(&transaction->plan, source, &platform->geometry,
		platform->fmap, platform->store, platform->bios, platform->names, platform->ranges,
		platform->count, transaction->floor, image, platform->program, platform->boot_slot);
	if (status)
		ass_capsule_source_close(source);
	else
		write_le32((uint8_t *)message + 8, ass_native_capsule_activate(transaction));
	return capsule_result(message, request.revision, status);
}
