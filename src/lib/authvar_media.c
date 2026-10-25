/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_media.h>
#include <boot/payload_mm_authvar_default_store.h>
#include <boot/payload_mm_authvar_service.h>
#include <string.h>

static bool error(uint64_t status)
{
	return (status & PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT) != 0;
}

static void write_u64(uint8_t *out, uint64_t value)
{
	memcpy(out, &value, sizeof(value));
}

static uint64_t verify_span(struct authvar_media *media, const struct authvar_media_io *io,
	uint32_t offset, const uint8_t *expected, size_t size)
{
	while (size) {
		size_t chunk = size < media->block_size ? size : media->block_size;
		uint64_t status = io->read(io->context, offset, media->verify, chunk);

		if (error(status))
			return status;
		for (size_t i = 0; i < chunk; i++)
			if (media->verify[i] != (expected ? expected[i] : 0xffU))
				return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		offset += (uint32_t)chunk;
		if (expected)
			expected += chunk;
		size -= chunk;
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static uint64_t program(struct authvar_media *media, const struct authvar_media_io *io,
	uint32_t offset, const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;

	if (offset > media->image_size || size > media->image_size - offset)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	while (size) {
		size_t chunk = media->block_size - offset % media->block_size;
		uint64_t status;

		if (chunk > size)
			chunk = size;
		status = io->read(io->context, offset, media->verify, chunk);
		if (error(status))
			return status;
		/* Full FTW images include erased padding that needs no programming. */
		if (memcmp(media->verify, bytes, chunk)) {
			status = io->write(io->context, offset, bytes, chunk);
			if (!error(status))
				status = verify_span(media, io, offset, bytes, chunk);
			if (error(status))
				return status;
		}
		offset += (uint32_t)chunk;
		bytes += chunk;
		size -= chunk;
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static uint64_t erase(struct authvar_media *media, const struct authvar_media_io *io,
	uint32_t offset, size_t size)
{
	if (offset % media->block_size || size % media->block_size ||
	    offset > media->image_size || size > media->image_size - offset)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	while (size) {
		uint64_t status;
		bool blank = true;

		status = io->read(io->context, offset, media->verify, media->block_size);
		if (error(status))
			return status;
		for (size_t i = 0; i < media->block_size; i++) {
			if (media->verify[i] != 0xffU) {
				blank = false;
				break;
			}
		}
		if (!blank) {
			status = io->erase(io->context, offset, media->block_size);
			if (!error(status))
				status = verify_span(media, io, offset, NULL, media->block_size);
			if (error(status))
				return status;
		}
		offset += media->block_size;
		size -= media->block_size;
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static uint64_t marker(struct authvar_media *media, const struct authvar_media_io *io,
	uint32_t offset, uint8_t expected, uint8_t wanted)
{
	uint8_t byte;
	uint64_t status = io->read(io->context, offset, &byte, 1);

	if (error(status))
		return status;
	if (byte != expected || (expected & wanted) != wanted)
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	return program(media, io, offset, &wanted, 1);
}

static uint32_t crc32(const uint8_t *bytes, size_t size)
{
	uint32_t crc = UINT32_MAX;

	while (size--) {
		crc ^= *bytes++;
		for (unsigned int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320U & (uint32_t)-(int32_t)(crc & 1U));
	}
	return ~crc;
}

static void empty_workspace(uint8_t *workspace, size_t size)
{
	uint32_t checksum;

	memset(workspace, 0xff, size);
	memcpy(workspace, payload_mm_authvar_ftw_working_block_guid, 16);
	write_u64(workspace + 24, size - 32U);
	checksum = crc32(workspace, 32);
	memcpy(workspace + 16, &checksum, sizeof(checksum));
}

#define STEP(operation) do { status = (operation); if (error(status)) return status; } while (0)

static uint64_t restore_workspace(struct authvar_media *media,
	const struct authvar_media_io *io, uint8_t *workspace)
{
	const struct payload_mm_authvar_fv_geometry *g = &media->plan.geometry;
	uint64_t status;

	workspace[20] = 0xffU;
	STEP(erase(media, io, g->working_offset, g->working_size));
	STEP(program(media, io, g->working_offset, workspace, g->working_size));
	return marker(media, io, g->working_offset + 20U, 0xffU, 0xfeU);
}

static uint64_t rebuild_workspace(struct authvar_media *media,
	const struct authvar_media_io *io, bool invalidate)
{
	const struct payload_mm_authvar_fv_geometry *g = &media->plan.geometry;
	uint64_t status;

	empty_workspace(media->workspace, g->working_size);
	STEP(erase(media, io, g->spare_offset, g->spare_size));
	STEP(program(media, io, g->spare_offset, media->workspace, g->working_size));
	STEP(marker(media, io, g->spare_offset + 20U, 0xffU, 0xfeU));
	if (invalidate)
		STEP(marker(media, io, g->working_offset + 20U, 0xfeU, 0xfcU));
	STEP(restore_workspace(media, io, media->workspace));
	return erase(media, io, g->spare_offset, g->spare_size);
}

static uint64_t finish_image(struct authvar_media *media, const struct authvar_media_io *io,
	const uint8_t *image)
{
	const struct payload_mm_authvar_fv_geometry *g = &media->plan.geometry;
	uint32_t queue = g->working_offset + media->plan.queue_offset;
	uint64_t status;

	STEP(erase(media, io, g->variable_offset, g->variable_size));
	STEP(program(media, io, g->variable_offset, image, g->variable_size));
	STEP(marker(media, io, queue + 40U, 0xfdU, 0xf9U));
	STEP(marker(media, io, queue, 0xfcU, 0xf8U));
	return erase(media, io, g->spare_offset, g->spare_size);
}

static uint64_t recover_once(struct authvar_media *media, const struct authvar_media_io *io)
{
	const struct payload_mm_authvar_ftw_plan *p = &media->plan;
	const struct payload_mm_authvar_fv_geometry *g = &p->geometry;
	uint32_t queue = g->working_offset + p->queue_offset;
	uint8_t header_state;
	uint64_t status;

	switch (p->action) {
	case PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE:
		return rebuild_workspace(media, io, false);
	case PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE:
		return rebuild_workspace(media, io, true);
	case PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED:
		return p->workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING ?
			erase(media, io, g->working_offset, g->working_size) :
			erase(media, io, g->spare_offset, g->spare_size);
	case PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE:
		return erase(media, io, g->spare_offset, g->spare_size);
	case PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD:
		header_state = media->image[queue];
		if (header_state == 0xfeU)
			return marker(media, io, queue, 0xfeU, 0xfaU);
		STEP(erase(media, io, g->spare_offset, g->spare_size));
		return marker(media, io, queue, 0xfcU, 0xf8U);
	case PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE:
		return finish_image(media, io, media->image + g->spare_offset);
	case PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW:
		STEP(marker(media, io, queue, 0xfcU, 0xf8U));
		return erase(media, io, g->spare_offset, g->spare_size);
	case PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE:
		memcpy(media->workspace, media->image + g->spare_offset, g->working_size);
		header_state = media->workspace[p->queue_offset];
		STEP(restore_workspace(media, io, media->workspace));
		if (p->queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD)
			STEP(marker(media, io, queue, header_state,
				header_state == 0xfeU ? 0xfaU : 0xf8U));
		return erase(media, io, g->spare_offset, g->spare_size);
	default:
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	}
}

static uint64_t recover(struct authvar_media *media,
	const struct authvar_media_io *io, bool read_needed, bool initializing)
{
	uint64_t status;

	/* Each recovery either settles a queue entry or rebuilds its workspace. */
	for (unsigned int pass = 0; pass < 8; pass++) {
		if (read_needed) {
			status = io->read(io->context, 0, media->image, media->image_size);
			if (error(status))
				return status;
		}
		if (payload_mm_authvar_ftw_plan(media->image, media->image_size,
		    media->block_size, &media->plan) != CB_SUCCESS)
			return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
		if (media->plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN)
			return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		status = recover_once(media, io);
		if (initializing && io->recovery)
			io->recovery(io->context, &media->plan, pass + 1U, status);
		if (error(status))
			return status;
		read_needed = true;
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
}

uint64_t authvar_media_commit(struct authvar_media *media,
	const struct authvar_media_io *io, const void *store, size_t size,
	const struct payload_mm_authvar_write_plan *append, const void *record)
{
	const struct payload_mm_authvar_fv_geometry *g;
	uint8_t metadata[80];
	uint32_t queue;
	uint64_t status;

	if (!media || !io || !io->read || !io->write || !io->erase ||
	    !store || size != media->store_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (media->failed)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	/* The engine owns admission; media refuses a stale source before mutation. */
	status = verify_span(media, io, 0, media->image, media->image_size);
	if (error(status))
		goto fail;
	g = &media->plan.geometry;
	memcpy(media->candidate, media->image, media->plan.fv_header_size);
	memmove(media->candidate + media->plan.fv_header_size, store, size);
	/* Preserve the FV and store contracts; the engine validates record bodies. */
	if (memcmp(media->candidate + media->plan.fv_header_size,
	    media->store, 28U))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (append && append->action == PAYLOAD_MM_AUTHVAR_WRITE_DELETE) {
		const struct payload_mm_authvar_write_step *step = &append->steps[0];
		uint32_t offset = step->offset;
		uint8_t *expected = media->candidate + media->plan.fv_header_size;

		if (append->step_count != 1U || step->kind != PAYLOAD_MM_AUTHVAR_WRITE_STATE ||
		    offset < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 2U || offset >= size ||
		    step->size != 1U || step->expected_state != PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
		    step->new_state != PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED ||
		    media->store[offset] != PAYLOAD_MM_AUTHVAR_STATE_ADDED)
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		memcpy(expected, media->store, size);
		expected[offset] = step->new_state;
		if (memcmp(expected, store, size))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		status = marker(media, io, media->plan.fv_header_size + offset,
			step->expected_state, step->new_state);
		if (!error(status))
			status = verify_span(media, io, media->plan.fv_header_size, store, size);
		if (error(status))
			goto fail;
		memcpy(media->store, store, size);
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	if (append != NULL) {
		const uint8_t *bytes = store, *body = record;
		uint32_t offset = append->record_offset, length = append->record_size;
		uint32_t physical = media->plan.fv_header_size + offset;
		bool replacing = append->step_count == 5U;
		size_t first = replacing ? 1U : 0U;
		const struct payload_mm_authvar_write_step *steps = append->steps + first;
		uint32_t old_state = append->steps[0].offset;
		uint8_t *expected = media->candidate + media->plan.fv_header_size;

		if (body == NULL || append->action != PAYLOAD_MM_AUTHVAR_WRITE_TAIL_APPEND ||
		    (append->step_count != 3U && !replacing) ||
		    offset > size || length > size - offset ||
		    length < PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE ||
		    steps[0].kind != PAYLOAD_MM_AUTHVAR_WRITE_RECORD ||
		    steps[0].offset != offset || steps[0].size != length ||
		    steps[0].expected_state != PAYLOAD_MM_AUTHVAR_STATE_ERASED ||
		    steps[0].new_state != PAYLOAD_MM_AUTHVAR_STATE_ERASED ||
		    steps[1].kind != PAYLOAD_MM_AUTHVAR_WRITE_STATE ||
		    steps[1].offset != offset + 2U || steps[1].size != 1U ||
		    steps[1].expected_state != PAYLOAD_MM_AUTHVAR_STATE_ERASED ||
		    steps[1].new_state != PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY ||
		    steps[2].kind != PAYLOAD_MM_AUTHVAR_WRITE_STATE ||
		    steps[2].offset != offset + 2U || steps[2].size != 1U ||
		    steps[2].expected_state != PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY ||
		    steps[2].new_state != PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
		    body[2] != PAYLOAD_MM_AUTHVAR_STATE_ERASED ||
		    bytes[offset + 2U] != PAYLOAD_MM_AUTHVAR_STATE_ADDED)
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		if (replacing &&
		    (old_state < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 2U || old_state >= offset ||
		     append->steps[0].kind != PAYLOAD_MM_AUTHVAR_WRITE_STATE ||
		     append->steps[0].size != 1U ||
		     append->steps[0].expected_state != PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
		     append->steps[0].new_state != PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION ||
		     append->steps[4].kind != PAYLOAD_MM_AUTHVAR_WRITE_STATE ||
		     append->steps[4].offset != old_state || append->steps[4].size != 1U ||
		     append->steps[4].expected_state != PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION ||
		     append->steps[4].new_state != PAYLOAD_MM_AUTHVAR_STATE_TRANSITION_DELETED ||
		     media->store[old_state] != PAYLOAD_MM_AUTHVAR_STATE_ADDED))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		memcpy(expected, media->store, size);
		memcpy(expected + offset, body, length);
		expected[offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
		if (replacing)
			expected[old_state] = PAYLOAD_MM_AUTHVAR_STATE_TRANSITION_DELETED;
		if (memcmp(expected, bytes, size))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		/* The body stays uncommitted until its complete readback succeeds. */
		status = verify_span(media, io, physical, NULL, length);
		if (!error(status) && replacing)
			status = marker(media, io, media->plan.fv_header_size + old_state,
				append->steps[0].expected_state, append->steps[0].new_state);
		if (!error(status)) status = program(media, io, physical, body, length);
		for (size_t i = first + 1U; !error(status) && i < append->step_count; i++)
			status = marker(media, io, media->plan.fv_header_size + append->steps[i].offset,
				append->steps[i].expected_state, append->steps[i].new_state);
		if (!error(status))
			status = verify_span(media, io, media->plan.fv_header_size, bytes, size);
		if (error(status))
			goto fail;
		memcpy(media->store, store, size);
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	if (media->plan.queue_offset > g->working_size ||
	    80U > g->working_size - media->plan.queue_offset)
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	queue = g->working_offset + media->plan.queue_offset;
	memset(metadata, 0xff, sizeof(metadata));
	memcpy(metadata + 4, payload_mm_authvar_ftw_coreboot_caller_guid, 16);
	write_u64(metadata + 24, 1);
	write_u64(metadata + 32, 0);
	write_u64(metadata + 48, 0);
	write_u64(metadata + 56, media->plan.fv_header_size);
	write_u64(metadata + 64, media->store_size);
	write_u64(metadata + 72, (uint64_t)-(int64_t)g->spare_offset);
	status = program(media, io, queue + 1, metadata + 1, 39);
	if (!error(status)) status = marker(media, io, queue, 0xffU, 0xfeU);
	if (!error(status)) status = marker(media, io, queue, 0xfeU, 0xfcU);
	if (!error(status)) status = program(media, io, queue + 41, metadata + 41, 39);
	if (!error(status)) status = erase(media, io, g->spare_offset, g->spare_size);
	if (!error(status))
		status = program(media, io, g->spare_offset, media->candidate, g->variable_size);
	if (!error(status)) status = marker(media, io, queue + 40, 0xffU, 0xfdU);
	if (!error(status)) status = finish_image(media, io, media->candidate);
	if (!error(status)) status = recover(media, io, true, false);
	if (!error(status) && memcmp(media->image + media->plan.fv_header_size,
	    store, size))
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (!error(status))
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
fail:
	/* A failed transaction is settled on next boot, never published from RAM. */
	media->failed = true;
	return status;
}

uint64_t authvar_media_initialize(struct authvar_media *media,
	const struct authvar_media_io *io)
{
	struct payload_mm_authvar_fv_geometry geometry;
	enum payload_mm_authvar_default_store_source source;
	bool read_needed = false;
	uint64_t status;

	if (!media || !io || !io->read || !io->write || !io->erase ||
	    !media->image || !media->candidate || !media->verify || !media->workspace ||
	    !media->block_size || media->candidate_size < media->image_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (media->image_size > UINT32_MAX)
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (!payload_mm_authvar_fv_geometry(&geometry, media->image_size, media->block_size))
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	status = io->read(io->context, 0, media->image, media->image_size);
	if (error(status))
		return status;
	source = payload_mm_authvar_default_store_compose(media->image,
		media->candidate, media->image_size, media->block_size);
	if (source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID)
		return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
	if (source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED ||
	    source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_NOR_SUBSET) {
		/* Bootstrap only proven erased/NOR-subset media, without erasing it. */
		status = program(media, io, 0, media->candidate, media->image_size);
		if (error(status))
			return status;
		read_needed = true;
	}
	status = recover(media, io, read_needed, true);
	if (error(status))
		return status;
	media->store = media->image + media->plan.fv_header_size;
	media->store_size = media->plan.variable_store_size;
	/* A previously initialized FTW workspace can accompany an empty store. */
	bool empty = true;

	for (size_t i = 28U; i < media->store_size; i++)
		empty &= media->store[i] == 0xffU;
	if (empty) {
		source = payload_mm_authvar_default_store_compose(media->image,
			media->candidate, media->image_size, media->block_size);
		if (source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID ||
		    media->plan.fv_header_size != PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE)
			return PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA;
		return authvar_media_commit(media, io,
			media->candidate + media->plan.fv_header_size,
			media->store_size, NULL, NULL);
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}
