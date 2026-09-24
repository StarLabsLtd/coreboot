/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar.h>
#include <boot/payload_mm_authvar_media.h>
#include <boot/payload_mm_authvar_smmstore.h>
#include <boot_device.h>
#include <commonlib/region.h>
#include <smmstore.h>
#include <spi_flash.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authenticated-variable SMMSTORE backend is SMM-only"
#endif

#define SMMSTORE_BACKEND_REVISION 1U

enum backend_transaction_state {
	BACKEND_IDLE = 0,
	BACKEND_BEGINNING,
	BACKEND_ACTIVE,
	BACKEND_ENDING,
};

struct backend_policy {
	const struct spi_flash *flash;
	uint64_t generation;
	uint32_t store_offset;
	uint32_t store_size;
	uint32_t erase_size;
};

struct backend_context {
	uint32_t revision;
	uint32_t size;
	struct backend_policy policy;
};

static struct {
	struct backend_policy policy;
	struct backend_policy sealed;
	struct backend_context context;
	struct payload_mm_authvar_media_port port;
	struct spi_flash_volatile_lease lease;
	uint32_t install_attempted;
	uint32_t transaction;
	uint32_t poisoned;
} backend;

static void poison(void)
{
	__atomic_store_n(&backend.poisoned, 1, __ATOMIC_RELEASE);
}

static bool policy_equal(void)
{
	return !memcmp(&backend.policy, &backend.sealed,
		sizeof(backend.policy));
}

static bool context_valid(const void *opaque)
{
	const struct backend_context *context = opaque;

	if (!context || context->revision != SMMSTORE_BACKEND_REVISION ||
	    context->size != sizeof(*context) ||
	    memcmp(&context->policy, &backend.sealed,
		sizeof(context->policy)) || !policy_equal()) {
		poison();
		return false;
	}
	return true;
}

static bool span_valid(uint32_t offset, size_t size, uint32_t *absolute)
{
	if (!size || size > UINT32_MAX || offset > backend.policy.store_size ||
	    size > backend.policy.store_size - offset ||
	    backend.policy.store_offset > UINT32_MAX - offset)
		return false;
	*absolute = backend.policy.store_offset + offset;
	return *absolute <= backend.sealed.flash->size &&
		size <= backend.sealed.flash->size - *absolute;
}

static bool buffer_disjoint(const void *buffer, size_t size)
{
	return buffer && size &&
		!payload_mm_authvar_buffers_overlap(buffer, size, &backend,
			sizeof(backend));
}

static enum payload_mm_authvar_media_result begin(const void *opaque,
	uint64_t *generation)
{
	uint32_t expected = BACKEND_IDLE;
	bool valid;

	if (!generation || !context_valid(opaque) ||
	    __atomic_load_n(&backend.poisoned, __ATOMIC_ACQUIRE) ||
	    !__atomic_compare_exchange_n(&backend.transaction, &expected,
		BACKEND_BEGINNING, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	*generation = 0;
	if (boot_device_spi_flash() != backend.sealed.flash ||
	    backend.sealed.flash->size < backend.policy.store_offset ||
	    backend.policy.store_size >
		backend.sealed.flash->size - backend.policy.store_offset ||
	    backend.sealed.flash->sector_size != backend.policy.erase_size ||
	    spi_flash_volatile_lease_begin(backend.sealed.flash,
		&backend.lease)) {
		poison();
		__atomic_store_n(&backend.transaction, BACKEND_IDLE,
			__ATOMIC_RELEASE);
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	valid = context_valid(opaque) &&
		!__atomic_load_n(&backend.poisoned, __ATOMIC_ACQUIRE);
	if (!valid) {
		(void)spi_flash_volatile_lease_end(&backend.lease);
		__atomic_store_n(&backend.transaction, BACKEND_IDLE,
			__ATOMIC_RELEASE);
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	*generation = backend.policy.generation;
	__atomic_store_n(&backend.transaction, BACKEND_ACTIVE, __ATOMIC_RELEASE);
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result read_media(const void *opaque,
	uint32_t offset, void *buffer, size_t size, size_t *completed)
{
	uint32_t absolute;
	int result;

	if (completed && !payload_mm_authvar_buffers_overlap(completed,
		sizeof(*completed), &backend, sizeof(backend)))
		*completed = 0;
	if (!completed || payload_mm_authvar_buffers_overlap(completed,
		sizeof(*completed), &backend, sizeof(backend)) ||
	    !buffer_disjoint(buffer, size) ||
	    !context_valid(opaque) ||
	    __atomic_load_n(&backend.transaction, __ATOMIC_ACQUIRE) !=
		BACKEND_ACTIVE || !span_valid(offset, size, &absolute)) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	result = spi_flash_volatile_lease_read(backend.sealed.flash,
		&backend.lease, absolute, size, buffer);
	if (result || !context_valid(opaque)) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	*completed = size;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result program(const void *opaque,
	uint32_t offset, const void *buffer, size_t size)
{
	uint32_t absolute;
	int result;

	if (!buffer_disjoint(buffer, size) || !context_valid(opaque) ||
	    __atomic_load_n(&backend.transaction, __ATOMIC_ACQUIRE) !=
		BACKEND_ACTIVE || !span_valid(offset, size, &absolute)) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	result = spi_flash_volatile_lease_write(backend.sealed.flash,
		&backend.lease, absolute, size, buffer);
	if (result || !context_valid(opaque)) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result erase(const void *opaque,
	uint32_t offset, size_t size)
{
	uint32_t absolute;
	int result;

	if (!context_valid(opaque) ||
	    __atomic_load_n(&backend.transaction, __ATOMIC_ACQUIRE) !=
		BACKEND_ACTIVE || size != backend.policy.erase_size ||
	    offset % backend.policy.erase_size ||
	    !span_valid(offset, size, &absolute) ||
	    absolute % backend.policy.erase_size) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	result = spi_flash_volatile_lease_erase(backend.sealed.flash,
		&backend.lease, absolute, size);
	if (result || !context_valid(opaque)) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result sync_media(const void *opaque)
{
	int result;
	bool valid = context_valid(opaque);

	if (__atomic_load_n(&backend.transaction, __ATOMIC_ACQUIRE) !=
	    BACKEND_ACTIVE)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	/* Attempt the hardware fence even after a prior operation poisoned policy. */
	result = spi_flash_volatile_lease_sync(backend.sealed.flash,
		&backend.lease);
	if (result || !valid || !context_valid(opaque) ||
	    __atomic_load_n(&backend.poisoned, __ATOMIC_ACQUIRE)) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result end(const void *opaque)
{
	uint32_t expected = BACKEND_ACTIVE;
	bool valid = context_valid(opaque);
	int result;

	if (!__atomic_compare_exchange_n(&backend.transaction, &expected,
		BACKEND_ENDING, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
		poison();
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	/* Cleanup uses only the private owner handle, never mutable context. */
	result = spi_flash_volatile_lease_end(&backend.lease);
	if (result || !valid || !context_valid(opaque) ||
	    __atomic_load_n(&backend.poisoned, __ATOMIC_ACQUIRE))
		poison();
	__atomic_store_n(&backend.transaction, BACKEND_IDLE, __ATOMIC_RELEASE);
	return result || __atomic_load_n(&backend.poisoned, __ATOMIC_ACQUIRE) ?
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR :
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum cb_err payload_mm_authvar_smmstore_install(void)
{
	struct payload_mm_authvar_contract contract;
	struct region_device store;
	const struct spi_flash *flash;
	uint32_t expected = 0;

	if (!__atomic_compare_exchange_n(&backend.install_attempted, &expected, 1,
		false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED) ||
	    !payload_mm_authvar_smram_buffer(&backend, sizeof(backend)) ||
	    !payload_mm_authvar_authority_snapshot(&contract) ||
	    smmstore_lookup_read_region(&store) < 0)
		return CB_ERR;
	flash = boot_device_spi_flash();
	if (!flash || contract.boot_media_size != flash->size ||
	    contract.boot_media_size > UINT32_MAX ||
	    contract.store_offset > UINT32_MAX ||
	    contract.store_size > UINT32_MAX ||
	    region_device_offset(&store) != contract.store_offset ||
	    region_device_sz(&store) != contract.store_size ||
	    contract.block_size != SMM_BLOCK_SIZE ||
	    contract.erase_size != flash->sector_size ||
	    contract.store_offset > flash->size ||
	    contract.store_size > flash->size - contract.store_offset)
		return CB_ERR;
	backend.policy = (struct backend_policy) {
		.flash = flash,
		.generation = contract.generation,
		.store_offset = (uint32_t)contract.store_offset,
		.store_size = (uint32_t)contract.store_size,
		.erase_size = contract.erase_size,
	};
	backend.sealed = backend.policy;
	backend.context = (struct backend_context) {
		.revision = SMMSTORE_BACKEND_REVISION,
		.size = sizeof(backend.context),
		.policy = backend.policy,
	};
	backend.port = (struct payload_mm_authvar_media_port) {
		.revision = PAYLOAD_MM_AUTHVAR_MEDIA_PORT_REVISION,
		.size = sizeof(backend.port),
		.begin = begin,
		.read = read_media,
		.program = program,
		.erase = erase,
		.sync = sync_media,
		.end = end,
		.context = &backend.context,
		.context_size = sizeof(backend.context),
	};
	if (!policy_equal() ||
	    payload_mm_authvar_media_install(&backend.port) != CB_SUCCESS)
		return CB_ERR;
	return CB_SUCCESS;
}
