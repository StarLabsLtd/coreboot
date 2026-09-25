/* SPDX-License-Identifier: GPL-2.0-only */

#include "mor_platform.h"

#include "mor_private_boundary.h"

#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <bootmem.h>
#include <commonlib/helpers.h>
#include <random.h>
#include <string.h>

#define MTL_MOR_PRIVATE_LIMIT_EXCLUSIVE (1ULL << 32)
#define MTL_MOR_PRIVATE_CALLBACK_STACK_BYTES 3072U

enum mtl_mor_private_phase {
	MTL_MOR_PRIVATE_EMPTY,
	MTL_MOR_PRIVATE_SEEDING,
	MTL_MOR_PRIVATE_SEEDED,
	MTL_MOR_PRIVATE_REGISTERING,
	MTL_MOR_PRIVATE_RESERVED,
	MTL_MOR_PRIVATE_PUBLISHING,
	MTL_MOR_PRIVATE_PUBLISHED,
	MTL_MOR_PRIVATE_RESOLVING,
	MTL_MOR_PRIVATE_RESOLVED,
	MTL_MOR_PRIVATE_COMPLETING,
	MTL_MOR_PRIVATE_TERMINAL,
	MTL_MOR_PRIVATE_POISONED,
};

static const struct bootmem_aligned_reservation_request transport_request = {
	.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
	.size = sizeof(transport_request),
	.bytes = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE,
	.alignment = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE,
	.limit_exclusive = MTL_MOR_PRIVATE_LIMIT_EXCLUSIVE,
	.tag = BM_MEM_TABLE,
};

static struct {
	struct bootmem_aligned_reservation_handle handle;
	uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE];
	uint8_t receipt_secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE];
	uint64_t generation;
	uint32_t phase;
	uint32_t channel_open;
	uint32_t close_claimed;
} private_boundary;

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool nonzero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return combined;
}

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!object_valid(first, first_size, 1) ||
	    !object_valid(second, second_size, 1))
		return true;
	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static bool boundary_object_valid(const void *object, size_t size,
	size_t alignment)
{
	return object_valid(object, size, alignment) &&
		!ranges_overlap(object, size, &private_boundary,
			sizeof(private_boundary));
}

static void scrub_secrets(void)
{
	scrub(private_boundary.owner, sizeof(private_boundary.owner));
	scrub(private_boundary.receipt_secret,
		sizeof(private_boundary.receipt_secret));
}

static void poison(void)
{
	scrub_secrets();
	__atomic_store_n(&private_boundary.phase, MTL_MOR_PRIVATE_POISONED,
		__ATOMIC_RELEASE);
}

static bool phase_claim(uint32_t expected, uint32_t next)
{
	if (__atomic_compare_exchange_n(&private_boundary.phase, &expected, next,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return true;
	poison();
	return false;
}

static enum cb_err seed(void *context, uint64_t generation,
	const uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE])
{
	uint64_t entropy[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE /
		sizeof(uint64_t)] = { 0 };
	enum cb_err status = CB_ERR;

	if (context != &private_boundary || !generation ||
	    !boundary_object_valid(owner, STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE, 1) ||
	    !nonzero(owner, STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE)) {
		poison();
		return CB_ERR;
	}
	if (!phase_claim(MTL_MOR_PRIVATE_EMPTY, MTL_MOR_PRIVATE_SEEDING))
		return CB_ERR;
	for (size_t index = 0; index < ARRAY_SIZE(entropy); index++)
		if (get_random_number_64(&entropy[index]) != CB_SUCCESS)
			goto cleanup;
	if (!nonzero(entropy, sizeof(entropy)))
		goto cleanup;
	private_boundary.generation = generation;
	memcpy(private_boundary.owner, owner, sizeof(private_boundary.owner));
	memcpy(private_boundary.receipt_secret, entropy,
		sizeof(private_boundary.receipt_secret));
	if (!phase_claim(MTL_MOR_PRIVATE_SEEDING, MTL_MOR_PRIVATE_SEEDED))
		goto cleanup;
	status = CB_SUCCESS;

cleanup:
	scrub(entropy, sizeof(entropy));
	if (status != CB_SUCCESS)
		poison();
	return status;
}

static enum cb_err reservations_register(void *context, uint64_t generation)
{
	if (context != &private_boundary || generation != private_boundary.generation ||
	    !generation) {
		poison();
		return CB_ERR;
	}
	if (!phase_claim(MTL_MOR_PRIVATE_SEEDED, MTL_MOR_PRIVATE_REGISTERING))
		return CB_ERR;
	if (bootmem_aligned_reservation_register(&transport_request,
		&private_boundary.handle)) {
		poison();
		return CB_ERR;
	}
	if (!phase_claim(MTL_MOR_PRIVATE_REGISTERING, MTL_MOR_PRIVATE_RESERVED))
		return CB_ERR;
	return CB_SUCCESS;
}

bool platform_payload_mm_authvar_mor_private_smi_required(void)
{
	const uint32_t phase = __atomic_load_n(&private_boundary.phase,
		__ATOMIC_ACQUIRE);

	return phase >= MTL_MOR_PRIVATE_RESERVED &&
		phase <= MTL_MOR_PRIVATE_RESOLVED;
}

bool platform_payload_mm_authvar_mor_private_smi_seed(
	struct payload_mm_authvar_mor_private_smi_seed *seed_output)
{
	struct payload_mm_authvar_mor_private_smi_seed candidate = { 0 };
	struct payload_mm_authvar_mor_private_smi_seed original;
	bool published = false;

	if (!boundary_object_valid(seed_output, sizeof(*seed_output),
		_Alignof(*seed_output))) {
		poison();
		return false;
	}
	if (!phase_claim(MTL_MOR_PRIVATE_RESERVED, MTL_MOR_PRIVATE_PUBLISHING))
		return false;
	memcpy(&original, seed_output, sizeof(original));
	if (!private_boundary.generation ||
	    !nonzero(private_boundary.owner, sizeof(private_boundary.owner)) ||
	    !nonzero(private_boundary.receipt_secret,
		sizeof(private_boundary.receipt_secret)) ||
	    !private_boundary.handle.opaque[0] ||
	    !private_boundary.handle.opaque[1])
		goto fail;
	candidate = (struct payload_mm_authvar_mor_private_smi_seed) {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_REVISION,
		.size = sizeof(candidate),
		.cold_boot_generation = private_boundary.generation,
		.page_handle = private_boundary.handle,
	};
	memcpy(candidate.capability, private_boundary.owner,
		sizeof(candidate.capability));
	memcpy(candidate.receipt_secret, private_boundary.receipt_secret,
		sizeof(candidate.receipt_secret));
	*seed_output = candidate;
	published = true;
	__atomic_store_n(&private_boundary.channel_open, 1, __ATOMIC_RELEASE);
	if (!phase_claim(MTL_MOR_PRIVATE_PUBLISHING, MTL_MOR_PRIVATE_PUBLISHED))
		goto fail;
	scrub(private_boundary.receipt_secret,
		sizeof(private_boundary.receipt_secret));
	scrub(&candidate, sizeof(candidate));
	scrub(&original, sizeof(original));
	return true;

fail:
	if (published) {
		memcpy(seed_output, &original, sizeof(original));
		__atomic_store_n(&private_boundary.channel_open, 0, __ATOMIC_RELEASE);
	}
	scrub(&candidate, sizeof(candidate));
	scrub(&original, sizeof(original));
	poison();
	return false;
}

static bool transport_valid(
	const struct bootmem_aligned_reservation *transport)
{
	return transport->size == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE &&
		transport->tag == BM_MEM_TABLE && !transport->reserved &&
		!(transport->base % PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE) &&
		transport->base < MTL_MOR_PRIVATE_LIMIT_EXCLUSIVE &&
		transport->size <= MTL_MOR_PRIVATE_LIMIT_EXCLUSIVE - transport->base;
}

static enum cb_err resolve(void *context, uint64_t generation,
	const uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE],
	struct bootmem_aligned_reservation *transport)
{
	struct payload_mm_authvar_mor_seal_channel channel = { 0 };
	struct bootmem_aligned_reservation candidate = { 0 };
	enum cb_err status = CB_ERR;

	if (context != &private_boundary || generation != private_boundary.generation ||
	    !boundary_object_valid(owner, STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE, 1) ||
	    !boundary_object_valid(transport, sizeof(*transport),
		_Alignof(*transport)) ||
	    ranges_overlap(owner, STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE,
		transport, sizeof(*transport)) ||
	    memcmp(owner, private_boundary.owner, sizeof(private_boundary.owner))) {
		poison();
		return CB_ERR;
	}
	if (!phase_claim(MTL_MOR_PRIVATE_PUBLISHED, MTL_MOR_PRIVATE_RESOLVING))
		return CB_ERR;
	if (bootmem_aligned_reservation_query(&private_boundary.handle,
		&candidate) || !transport_valid(&candidate) ||
	    payload_mm_authvar_mor_private_smi_seal_channel_resolve(&channel) !=
		CB_SUCCESS ||
	    channel.transport_base != candidate.base +
		offsetof(struct payload_mm_authvar_mor_private_smi_request, seal) ||
	    channel.transport_size !=
		sizeof(struct payload_mm_authvar_mor_seal_request) ||
	    channel.caller != payload_mm_authvar_mor_private_smi_identity(
		private_boundary.owner, private_boundary.generation) ||
	    channel.caller_context != payload_mm_authvar_mor_private_smi_cookie(
		channel.caller, candidate.base, private_boundary.generation, 0,
		CONFIG_MAX_CPUS) ||
	    memcmp(channel.capability, private_boundary.owner,
		sizeof(channel.capability)) ||
	    channel.transport_base < candidate.base ||
	    channel.transport_size > candidate.size ||
	    channel.transport_base - candidate.base >
		candidate.size - channel.transport_size)
		goto cleanup;
	if (!phase_claim(MTL_MOR_PRIVATE_RESOLVING, MTL_MOR_PRIVATE_RESOLVED))
		goto cleanup;
	*transport = candidate;
	status = CB_SUCCESS;

cleanup:
	scrub(&candidate, sizeof(candidate));
	scrub(&channel, sizeof(channel));
	if (status != CB_SUCCESS)
		poison();
	return status;
}

static enum cb_err complete(void *context,
	const struct payload_mm_authvar_mor_grant *grant)
{
	uint32_t expected = MTL_MOR_PRIVATE_RESOLVED;
	enum cb_err status;

	if (context != &private_boundary ||
	    !boundary_object_valid(grant, sizeof(*grant), _Alignof(*grant))) {
		poison();
		return CB_ERR;
	}
	if (!__atomic_compare_exchange_n(&private_boundary.phase, &expected,
		MTL_MOR_PRIVATE_COMPLETING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE)) {
		poison();
		return CB_ERR;
	}
	expected = 0;
	if (!__atomic_compare_exchange_n(&private_boundary.close_claimed,
		&expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		if (__atomic_exchange_n(&private_boundary.channel_open, 0,
			__ATOMIC_ACQ_REL))
			(void)payload_mm_authvar_mor_private_smi_close_unused();
		poison();
		return CB_ERR;
	}
	status = payload_mm_authvar_mor_private_smi_send_install(grant);
	__atomic_store_n(&private_boundary.channel_open, 0, __ATOMIC_RELEASE);
	scrub_secrets();
	__atomic_store_n(&private_boundary.phase, status == CB_SUCCESS ?
		MTL_MOR_PRIVATE_TERMINAL : MTL_MOR_PRIVATE_POISONED,
		__ATOMIC_RELEASE);
	return status;
}

static enum cb_err close(void *context)
{
	uint32_t phase;
	uint32_t expected;
	bool channel_open;

	if (context != &private_boundary)
		return CB_ERR;
	phase = __atomic_load_n(&private_boundary.phase, __ATOMIC_ACQUIRE);
	if (phase == MTL_MOR_PRIVATE_SEEDING ||
	    phase == MTL_MOR_PRIVATE_REGISTERING ||
	    phase == MTL_MOR_PRIVATE_PUBLISHING ||
	    phase == MTL_MOR_PRIVATE_RESOLVING ||
	    phase == MTL_MOR_PRIVATE_COMPLETING ||
	    phase == MTL_MOR_PRIVATE_TERMINAL)
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&private_boundary.phase, &phase,
		MTL_MOR_PRIVATE_TERMINAL, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return CB_ERR;
	expected = 0;
	if (!__atomic_compare_exchange_n(&private_boundary.close_claimed,
		&expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		poison();
		return CB_ERR;
	}
	channel_open = __atomic_exchange_n(&private_boundary.channel_open, 0,
		__ATOMIC_ACQ_REL);
	scrub_secrets();
	if (!channel_open)
		return CB_SUCCESS;
	if (payload_mm_authvar_mor_private_smi_close_unused() == CB_SUCCESS)
		return CB_SUCCESS;
	poison();
	return CB_ERR;
}

bool starbook_mtl_mor_private_boundary(
	struct starbook_mtl_mor_private_boundary_ops *ops)
{
	if (!boundary_object_valid(ops, sizeof(*ops), _Alignof(*ops)))
		return false;
	*ops = (struct starbook_mtl_mor_private_boundary_ops) {
		.revision = STARBOOK_MTL_MOR_PRIVATE_BOUNDARY_REVISION,
		.size = sizeof(*ops),
		.context = &private_boundary,
		.context_size = sizeof(private_boundary),
		.callback_stack_bytes = MTL_MOR_PRIVATE_CALLBACK_STACK_BYTES,
		.arena_seed = seed,
		.reservations_register = reservations_register,
		.resolve = resolve,
		.complete = complete,
		.close = close,
	};
	return true;
}

#if ENV_TEST
void starbook_mtl_mor_private_boundary_reset_test(void)
{
	memset(&private_boundary, 0, sizeof(private_boundary));
}

bool starbook_mtl_mor_private_boundary_secrets_zero_test(void)
{
	return !nonzero(private_boundary.owner, sizeof(private_boundary.owner)) &&
		!nonzero(private_boundary.receipt_secret,
			sizeof(private_boundary.receipt_secret));
}
#endif
