/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_media.h>
#include <boot/payload_mm_authvar_service.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authenticated-variable media must only be built in SMM"
#endif

struct media_policy {
	struct payload_mm_authvar_media_port port;
	struct payload_mm_authvar_contract contract;
	struct payload_mm_authvar_ftw_geometry geometry;
	uint8_t context[PAYLOAD_MM_AUTHVAR_MEDIA_CONTEXT_CAPACITY]
		__aligned(__BIGGEST_ALIGNMENT__);
};

enum transaction_state {
	TRANSACTION_IDLE = 0,
	TRANSACTION_BEGINNING,
	TRANSACTION_ACTIVE,
	TRANSACTION_ENDING,
};

static struct {
	struct media_policy policy;
	struct media_policy sealed;
	uint64_t generation;
	uint64_t token;
	uint64_t next_token;
	uint64_t cache_generation;
	uint32_t install_attempted;
	uint32_t transaction;
	uint32_t callback_active;
	uint32_t poisoned;
	uint32_t fail_closed;
	bool installed;
	uint32_t cache_bound;
	uint8_t cleanup_context[PAYLOAD_MM_AUTHVAR_MEDIA_CONTEXT_CAPACITY]
		__aligned(__BIGGEST_ALIGNMENT__);
	uint8_t scratch[4][PAYLOAD_MM_AUTHVAR_MEDIA_SCRATCH_CAPACITY];
} media;

static void poison(void)
{
	__atomic_store_n(&media.poisoned, 1, __ATOMIC_RELEASE);
	__atomic_store_n(&media.cache_bound, 0, __ATOMIC_RELEASE);
}

static bool result_valid(enum payload_mm_authvar_media_result result)
{
	return result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ||
		result == PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED ||
		result == PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED ||
		result == PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

static bool policy_equal(void)
{
	struct payload_mm_authvar_media_port active = media.policy.port;
	struct payload_mm_authvar_media_port sealed = media.sealed.port;

	active.context = NULL;
	sealed.context = NULL;
	return !memcmp(&active, &sealed, sizeof(active)) &&
		!memcmp(&media.policy.contract, &media.sealed.contract,
			sizeof(media.policy.contract)) &&
		!memcmp(&media.policy.geometry, &media.sealed.geometry,
			sizeof(media.policy.geometry)) &&
		!memcmp(media.policy.context, media.sealed.context,
			sizeof(media.policy.context));
}

static bool policy_intact(void)
{
	if (!policy_equal()) {
		poison();
		return false;
	}
	return true;
}

static bool policy_unchanged(void)
{
	return policy_intact() &&
		!__atomic_load_n(&media.poisoned, __ATOMIC_ACQUIRE) &&
		!__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE);
}

static bool overlaps_media(const void *buffer, size_t size)
{
	return payload_mm_authvar_buffers_overlap(buffer, size, &media,
		sizeof(media));
}

static bool protected_buffer(const void *buffer, size_t size)
{
	return buffer && size && payload_mm_authvar_smram_buffer(buffer, size) &&
		!overlaps_media(buffer, size);
}

static bool geometry_valid(const struct payload_mm_authvar_contract *contract,
	const struct payload_mm_authvar_ftw_geometry *geometry)
{
	uint64_t end;

	if (contract->store_size > UINT32_MAX ||
	    geometry->block_size != contract->block_size ||
	    geometry->variable_offset ||
	    geometry->variable_size % contract->erase_size ||
	    geometry->working_offset % contract->erase_size ||
	    geometry->working_size % contract->erase_size ||
	    geometry->spare_offset % contract->erase_size ||
	    geometry->spare_size % contract->erase_size)
		return false;
	end = (uint64_t)geometry->spare_offset + geometry->spare_size;
	return geometry->working_offset == geometry->variable_size &&
		geometry->spare_offset == geometry->working_offset +
			geometry->working_size && end == contract->store_size;
}

static bool port_valid(const struct payload_mm_authvar_media_port *port)
{
	return port->revision == PAYLOAD_MM_AUTHVAR_MEDIA_PORT_REVISION &&
		port->size == sizeof(*port) && port->begin && port->read &&
		port->program && port->erase && port->sync && port->end &&
		((port->context == NULL) == (port->context_size == 0)) &&
		port->context_size <= PAYLOAD_MM_AUTHVAR_MEDIA_CONTEXT_CAPACITY;
}

static bool callbacks_protected(const struct payload_mm_authvar_media_port *port)
{
	return payload_mm_authvar_smram_buffer(
		(const void *)(uintptr_t)port->begin, 1) &&
		payload_mm_authvar_smram_buffer(
			(const void *)(uintptr_t)port->read, 1) &&
		payload_mm_authvar_smram_buffer(
			(const void *)(uintptr_t)port->program, 1) &&
		payload_mm_authvar_smram_buffer(
			(const void *)(uintptr_t)port->erase, 1) &&
		payload_mm_authvar_smram_buffer(
			(const void *)(uintptr_t)port->sync, 1) &&
		payload_mm_authvar_smram_buffer(
			(const void *)(uintptr_t)port->end, 1);
}

static bool callback_enter(void)
{
	uint32_t expected = 0;

	if (!__atomic_compare_exchange_n(&media.callback_active, &expected, 1,
		false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		poison();
		return false;
	}
	return true;
}

static void callback_leave(void)
{
	__atomic_store_n(&media.callback_active, 0, __ATOMIC_RELEASE);
}

static bool session_valid(uint64_t generation, uint64_t token)
{
	if (__atomic_load_n(&media.callback_active, __ATOMIC_ACQUIRE)) {
		poison();
		return false;
	}
	return __atomic_load_n(&media.transaction, __ATOMIC_ACQUIRE) ==
		TRANSACTION_ACTIVE &&
		generation && generation == media.generation && token &&
		token == media.token && policy_unchanged();
}

static bool callback_reentry(void)
{
	if (!__atomic_load_n(&media.callback_active, __ATOMIC_ACQUIRE))
		return false;
	poison();
	return true;
}

static bool session_owned(uint64_t generation, uint64_t token)
{
	return __atomic_load_n(&media.transaction, __ATOMIC_ACQUIRE) ==
		TRANSACTION_ACTIVE && generation && generation == media.generation &&
		token && token == media.token;
}

static bool span_valid(uint32_t offset, size_t size)
{
	return size && size <= UINT32_MAX &&
		offset <= media.policy.contract.store_size &&
		size <= media.policy.contract.store_size - offset;
}

static enum payload_mm_authvar_media_result read_backend(uint32_t offset,
	void *buffer, size_t size, bool cleanup)
{
	enum payload_mm_authvar_media_result result;
	size_t completed = 0;

	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE) ||
	    !callback_enter())
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	result = media.policy.port.read(media.policy.port.context, offset, buffer,
		size, &completed);
	callback_leave();
	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (!result_valid(result) || !policy_intact()) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	if (!cleanup && __atomic_load_n(&media.poisoned, __ATOMIC_ACQUIRE))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS || completed != size)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static bool sync_backend(void)
{
	enum payload_mm_authvar_media_result result;

	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE) ||
	    !callback_enter())
		return false;
	result = media.policy.port.sync(media.policy.port.context);
	callback_leave();
	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE))
		return false;
	if (!result_valid(result) || !policy_intact()) {
		poison();
		return false;
	}
	return result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static const void *sealed_context_copy(void)
{
	size_t size = media.sealed.port.context_size;

	if (!size)
		return NULL;
	memcpy(media.cleanup_context, media.sealed.context, size);
	return media.cleanup_context;
}

static bool sealed_context_unchanged(void)
{
	return !memcmp(media.cleanup_context, media.sealed.context,
		media.sealed.port.context_size);
}

static bool sync_backend_sealed(void)
{
	enum payload_mm_authvar_media_result result;
	const void *context = sealed_context_copy();

	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE) ||
	    !callback_enter())
		return false;
	result = media.sealed.port.sync(context);
	callback_leave();
	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE))
		return false;
	if (!result_valid(result) || result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ||
	    !sealed_context_unchanged()) {
		poison();
		return false;
	}
	return true;
}

static bool read_backend_sealed(uint32_t offset, void *buffer, size_t size)
{
	enum payload_mm_authvar_media_result result;
	const void *context = sealed_context_copy();
	size_t completed = 0;

	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE) ||
	    !callback_enter())
		return false;
	result = media.sealed.port.read(context, offset, buffer, size, &completed);
	callback_leave();
	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE))
		return false;
	if (!result_valid(result) || result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ||
	    completed != size || !sealed_context_unchanged()) {
		poison();
		return false;
	}
	return true;
}

static bool sealed_sync_readback(uint32_t offset, void *buffer, size_t size)
{
	bool synced = sync_backend_sealed();
	bool read;

	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE))
		return false;
	read = read_backend_sealed(offset, buffer, size);

	return synced && read;
}

static bool end_backend(void)
{
	enum payload_mm_authvar_media_result result;
	const void *context = sealed_context_copy();

	if (!callback_enter())
		return false;
	/* The sealed copy remains callable even after active-policy corruption. */
	result = media.sealed.port.end(context);
	callback_leave();
	if (!result_valid(result) || result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ||
	    !sealed_context_unchanged() || !policy_intact() ||
	    __atomic_load_n(&media.poisoned, __ATOMIC_ACQUIRE)) {
		poison();
		return false;
	}
	return true;
}

enum cb_err payload_mm_authvar_media_install(
	const struct payload_mm_authvar_media_port *trusted_port)
{
	struct payload_mm_authvar_media_port port;
	uint8_t context_copy[PAYLOAD_MM_AUTHVAR_MEDIA_CONTEXT_CAPACITY];
	uint32_t expected = 0;

	if (!__atomic_compare_exchange_n(&media.install_attempted, &expected, 1,
		false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
		return CB_ERR;
	if (!trusted_port ||
	    !payload_mm_authvar_smram_buffer(trusted_port, sizeof(*trusted_port)) ||
	    overlaps_media(trusted_port, sizeof(*trusted_port)) ||
	    !payload_mm_authvar_smram_buffer(&media, sizeof(media)))
		return CB_ERR;
	memcpy(&port, trusted_port, sizeof(port));
	if (!port_valid(&port) || !callbacks_protected(&port) ||
	    (port.context_size &&
	     (overlaps_media(port.context, port.context_size) ||
	      !payload_mm_authvar_smram_buffer(port.context,
		port.context_size))))
		return CB_ERR;
	if (port.context_size)
		memcpy(context_copy, port.context, port.context_size);
	if (memcmp(trusted_port, &port, sizeof(port)) ||
	    (port.context_size &&
	     memcmp(port.context, context_copy, port.context_size)))
		return CB_ERR;
	memset(&media.policy, 0, sizeof(media.policy));
	if (!payload_mm_authvar_authority_snapshot(&media.policy.contract) ||
	    media.policy.contract.erase_size >
		PAYLOAD_MM_AUTHVAR_MEDIA_SCRATCH_CAPACITY ||
	    payload_mm_authvar_ftw_geometry(&media.policy.geometry,
		media.policy.contract.store_size,
		media.policy.contract.block_size) != CB_SUCCESS ||
	    !geometry_valid(&media.policy.contract, &media.policy.geometry))
		return CB_ERR;
	media.policy.port = port;
	if (port.context_size) {
		memcpy(media.policy.context, context_copy, port.context_size);
		media.policy.port.context = media.policy.context;
	}
	media.sealed = media.policy;
	if (port.context_size)
		media.sealed.port.context = media.sealed.context;
	if (!policy_unchanged())
		return CB_ERR;
	media.installed = true;
	return CB_SUCCESS;
}

bool payload_mm_authvar_media_available(void)
{
	return media.installed &&
		!__atomic_load_n(&media.poisoned, __ATOMIC_ACQUIRE) && policy_unchanged();
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_begin(
	uint64_t *generation, uint64_t *token)
{
	enum payload_mm_authvar_media_result result;
	uint64_t external_generation = 0;
	uint32_t expected = 0;
	bool valid;

	if (!media.installed)
		return PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED;
	if (callback_reentry())
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (!generation || !token || !protected_buffer(generation,
		sizeof(*generation)) || !protected_buffer(token, sizeof(*token)) ||
	    payload_mm_authvar_buffers_overlap(generation, sizeof(*generation),
		token, sizeof(*token)) || !policy_unchanged())
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	*generation = 0;
	*token = 0;
	if (!__atomic_compare_exchange_n(&media.transaction, &expected,
		TRANSACTION_BEGINNING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_RELAXED)) {
		if (__atomic_load_n(&media.callback_active, __ATOMIC_ACQUIRE))
			poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	if (!callback_enter()) {
		__atomic_store_n(&media.transaction, TRANSACTION_IDLE,
			__ATOMIC_RELEASE);
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	result = media.policy.port.begin(media.policy.port.context,
		&external_generation);
	callback_leave();
	valid = result_valid(result) && policy_unchanged();
	if (!valid || result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ||
	    !external_generation || media.next_token == UINT64_MAX) {
		if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS && !end_backend())
			result = PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		__atomic_store_n(&media.transaction, TRANSACTION_IDLE,
			__ATOMIC_RELEASE);
		if (!valid || result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		return result == PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED ? result :
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	media.generation = external_generation;
	media.token = ++media.next_token;
	if (__atomic_load_n(&media.cache_bound, __ATOMIC_ACQUIRE) &&
	    media.cache_generation != external_generation)
		__atomic_store_n(&media.cache_bound, 0, __ATOMIC_RELEASE);
	*generation = media.generation;
	*token = media.token;
	__atomic_store_n(&media.transaction, TRANSACTION_ACTIVE,
		__ATOMIC_RELEASE);
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_read(
	uint64_t generation, uint64_t token, uint32_t offset, void *buffer,
	size_t size)
{
	uint8_t *snapshot = media.scratch[0];

	if (callback_reentry())
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (!protected_buffer(buffer, size) ||
	    size > PAYLOAD_MM_AUTHVAR_MEDIA_SCRATCH_CAPACITY)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	memset(buffer, 0, size);
	if (!session_valid(generation, token) || !span_valid(offset, size) ||
	    read_backend(offset, snapshot, size, false) !=
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	memcpy(buffer, snapshot, size);
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_program(
	uint64_t generation, uint64_t token, uint32_t offset, const void *buffer,
	size_t size)
{
	uint8_t *wanted = media.scratch[0];
	uint8_t *before = media.scratch[1];
	uint8_t *after = media.scratch[2];
	uint8_t *transfer = media.scratch[3];
	enum payload_mm_authvar_media_result result;
	bool synced;
	bool verified;

	if (callback_reentry() || !protected_buffer(buffer, size) ||
	    size > PAYLOAD_MM_AUTHVAR_MEDIA_SCRATCH_CAPACITY ||
	    !session_valid(generation, token) || !span_valid(offset, size))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	memcpy(wanted, buffer, size);
	if (read_backend(offset, before, size, false) !=
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	for (size_t i = 0; i < size; i++) {
		if ((before[i] & wanted[i]) != wanted[i])
			return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	__atomic_store_n(&media.cache_bound, 0, __ATOMIC_RELEASE);
	memcpy(transfer, wanted, size);
	if (!callback_enter())
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	result = media.policy.port.program(media.policy.port.context, offset,
		transfer, size);
	callback_leave();
	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (!policy_equal()) {
		poison();
		(void)sealed_sync_readback(offset, after, size);
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	if (!result_valid(result) || memcmp(transfer, wanted, size))
		poison();
	synced = sync_backend();
	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (!policy_equal()) {
		poison();
		verified = sealed_sync_readback(offset, after, size);
		synced = false;
	} else {
		verified = read_backend(offset, after, size, true) ==
			PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
	}
	if (!synced || !verified || !result_valid(result) ||
	    memcmp(transfer, wanted, size)) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	if (__atomic_load_n(&media.poisoned, __ATOMIC_ACQUIRE))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (!memcmp(after, wanted, size))
		return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED &&
	    !memcmp(after, before, size))
		return PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED;
	if (!memcmp(after, before, size))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	poison();
	return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

static bool erase_authorized(uint32_t offset, size_t size)
{
	const struct payload_mm_authvar_ftw_geometry *geometry =
		&media.policy.geometry;
	const uint32_t starts[] = { geometry->variable_offset,
		geometry->working_offset, geometry->spare_offset };
	const uint32_t sizes[] = { geometry->variable_size,
		geometry->working_size, geometry->spare_size };
	uint64_t end = (uint64_t)offset + size;

	if (!span_valid(offset, size) ||
	    size != media.policy.contract.erase_size ||
	    offset % media.policy.contract.erase_size)
		return false;
	for (size_t i = 0; i < ARRAY_SIZE(starts); i++) {
		if (offset >= starts[i] && end <= (uint64_t)starts[i] + sizes[i])
			return true;
	}
	return false;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_erase(
	uint64_t generation, uint64_t token, uint32_t offset, size_t size)
{
	uint8_t *before = media.scratch[0];
	uint8_t *after = media.scratch[1];
	enum payload_mm_authvar_media_result result;
	bool synced;
	bool verified;

	if (callback_reentry() || !session_valid(generation, token) ||
	    !erase_authorized(offset, size))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (read_backend(offset, before, size, false) !=
	    PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	__atomic_store_n(&media.cache_bound, 0, __ATOMIC_RELEASE);
	if (!callback_enter())
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	result = media.policy.port.erase(media.policy.port.context, offset, size);
	callback_leave();
	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (!policy_equal()) {
		poison();
		(void)sealed_sync_readback(offset, after, size);
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	if (!result_valid(result))
		poison();
	synced = sync_backend();
	if (__atomic_load_n(&media.fail_closed, __ATOMIC_ACQUIRE))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (!policy_equal()) {
		poison();
		verified = sealed_sync_readback(offset, after, size);
		synced = false;
	} else {
		verified = read_backend(offset, after, size, true) ==
			PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
	}
	if (!synced || !verified || !result_valid(result)) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	if (__atomic_load_n(&media.poisoned, __ATOMIC_ACQUIRE))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	for (size_t i = 0; i < size; i++) {
		if (after[i] != 0xff) {
			if (result == PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED &&
			    !memcmp(after, before, size))
				return PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED;
			if (!memcmp(after, before, size))
				return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
			poison();
			return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		}
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_end(
	uint64_t generation, uint64_t token)
{
	uint32_t expected = TRANSACTION_ACTIVE;
	bool ended;

	if (__atomic_load_n(&media.callback_active, __ATOMIC_ACQUIRE)) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	if (!session_owned(generation, token) ||
	    !__atomic_compare_exchange_n(&media.transaction, &expected,
		TRANSACTION_ENDING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_RELAXED))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	ended = end_backend();
	media.generation = 0;
	media.token = 0;
	__atomic_store_n(&media.transaction, TRANSACTION_IDLE, __ATOMIC_RELEASE);
	return ended ? PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS :
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_fail_closed(
	uint64_t generation, uint64_t token)
{
	/* Misuse of this internal terminal path is itself a fail-closed event. */
	__atomic_store_n(&media.fail_closed, 1, __ATOMIC_RELEASE);
	if (__atomic_load_n(&media.callback_active, __ATOMIC_ACQUIRE) ||
	    !session_owned(generation, token)) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	poison();
	return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

void payload_mm_authvar_media_cache_bind(uint64_t generation, uint64_t token)
{
	if (!session_valid(generation, token)) {
		__atomic_store_n(&media.cache_bound, 0, __ATOMIC_RELEASE);
		return;
	}
	media.cache_generation = generation;
	__atomic_store_n(&media.cache_bound, 1, __ATOMIC_RELEASE);
}

void payload_mm_authvar_media_cache_invalidate(void)
{
	__atomic_store_n(&media.cache_bound, 0, __ATOMIC_RELEASE);
}

bool payload_mm_authvar_media_cache_valid(uint64_t generation, uint64_t token)
{
	return session_valid(generation, token) &&
		__atomic_load_n(&media.cache_bound, __ATOMIC_ACQUIRE) &&
		media.cache_generation == generation;
}

uint64_t payload_mm_authvar_media_result_status(
	enum payload_mm_authvar_media_result result)
{
	switch (result) {
	case PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS:
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	case PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED:
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	case PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED:
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	case PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR:
	default:
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
}
