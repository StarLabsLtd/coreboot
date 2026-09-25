/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <bootmem.h>
#include <pthread.h>
#include <string.h>

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static unsigned int random_calls;
static unsigned int random_fail_at;
static unsigned int register_calls;
static unsigned int query_calls;
static unsigned int resolve_calls;
static unsigned int install_calls;
static unsigned int close_calls;
static bool bad_query;
static bool query_above_limit;
static bool bad_channel;
static enum cb_err install_status;
static struct payload_mm_authvar_mor_private_smi_seed published_seed;

struct close_call {
	const struct starbook_mtl_mor_private_boundary_ops *ops;
	enum cb_err status;
};

enum cb_err get_random_number_64(uint64_t *value)
{
	random_calls++;
	if (random_calls == random_fail_at)
		return CB_ERR;
	*value = 0x1020304050607000ULL + random_calls;
	return CB_SUCCESS;
}

int bootmem_aligned_reservation_register(
	const struct bootmem_aligned_reservation_request *request,
	struct bootmem_aligned_reservation_handle *handle)
{
	register_calls++;
	assert(request->revision == BOOTMEM_ALIGNED_RESERVATION_REVISION);
	assert(request->size == sizeof(*request));
	assert(request->bytes == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE);
	assert(request->alignment == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE);
	assert(request->limit_exclusive == (1ULL << 32));
	assert(request->tag == BM_MEM_TABLE && !request->reserved);
	*handle = (struct bootmem_aligned_reservation_handle) {
		.opaque = { 0x12345678, 0x9abcdef0 },
	};
	return 0;
}

int bootmem_aligned_reservation_query(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_aligned_reservation *reservation)
{
	query_calls++;
	assert(!memcmp(handle, &published_seed.page_handle, sizeof(*handle)));
	*reservation = (struct bootmem_aligned_reservation) {
		.base = query_above_limit ? (1ULL << 32) : 0x400000,
		.size = bad_query ? 0x2000 :
			PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE,
		.tag = BM_MEM_TABLE,
	};
	return 0;
}

enum cb_err payload_mm_authvar_mor_private_smi_seal_channel_resolve(
	struct payload_mm_authvar_mor_seal_channel *channel)
{
	const uint64_t identity = payload_mm_authvar_mor_private_smi_identity(
		published_seed.capability, published_seed.cold_boot_generation);

	resolve_calls++;
	*channel = (struct payload_mm_authvar_mor_seal_channel) {
		.transport_base = 0x400000 +
			offsetof(struct payload_mm_authvar_mor_private_smi_request, seal),
		.transport_size = sizeof(struct payload_mm_authvar_mor_seal_request),
		.caller = identity,
		.caller_context = payload_mm_authvar_mor_private_smi_cookie(identity,
			0x400000, published_seed.cold_boot_generation, 0,
			CONFIG_MAX_CPUS),
	};
	memcpy(channel->capability, published_seed.capability,
		sizeof(channel->capability));
	if (bad_channel)
		channel->caller_context++;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_private_smi_send_install(
	const struct payload_mm_authvar_mor_grant *grant)
{
	assert(grant);
	install_calls++;
	return install_status;
}

enum cb_err payload_mm_authvar_mor_private_smi_close_unused(void)
{
	close_calls++;
	return CB_SUCCESS;
}

#ifndef MOR_PRIVATE_BOUNDARY_SOURCE
#define MOR_PRIVATE_BOUNDARY_SOURCE \
	"../../src/mainboard/starlabs/starbook/variants/mtl/mor_private_boundary.c"
#endif
#include MOR_PRIVATE_BOUNDARY_SOURCE

static void *concurrent_close(void *argument)
{
	struct close_call *call = argument;

	call->status = call->ops->close(call->ops->context);
	return NULL;
}

static void reset(void)
{
	starbook_mtl_mor_private_boundary_reset_test();
	memset(&published_seed, 0, sizeof(published_seed));
	random_calls = 0;
	random_fail_at = 0;
	register_calls = 0;
	query_calls = 0;
	resolve_calls = 0;
	install_calls = 0;
	close_calls = 0;
	bad_query = false;
	query_above_limit = false;
	bad_channel = false;
	install_status = CB_SUCCESS;
}

static void prepare(struct starbook_mtl_mor_private_boundary_ops *ops,
	uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE])
{
	memset(owner, 0xa5, STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE);
	assert(starbook_mtl_mor_private_boundary(ops));
	assert(ops->revision == STARBOOK_MTL_MOR_PRIVATE_BOUNDARY_REVISION);
	assert(ops->size == sizeof(*ops));
	assert(ops->context && ops->context_size);
	assert(ops->callback_stack_bytes <=
		STARBOOK_MTL_MOR_PRIVATE_CALLBACK_STACK_MAX);
	assert(ops->arena_seed(ops->context, 7, owner) == CB_SUCCESS);
	assert(random_calls == 4);
}

static void publish(struct starbook_mtl_mor_private_boundary_ops *ops)
{
	assert(ops->reservations_register(ops->context, 7) == CB_SUCCESS);
	assert(register_calls == 1);
	assert(platform_payload_mm_authvar_mor_private_smi_required());
	assert(platform_payload_mm_authvar_mor_private_smi_seed(&published_seed));
	assert(published_seed.cold_boot_generation == 7);
	assert(published_seed.page_handle.opaque[0] == 0x12345678);
	assert(!nonzero(private_boundary.receipt_secret,
		sizeof(private_boundary.receipt_secret)));
}

static void test_install(void)
{
	struct starbook_mtl_mor_private_boundary_ops ops;
	struct bootmem_aligned_reservation transport = { 0 };
	struct payload_mm_authvar_mor_grant grant = { 0 };
	uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE];

	reset();
	prepare(&ops, owner);
	publish(&ops);
	assert(ops.resolve(ops.context, 7, owner, &transport) == CB_SUCCESS);
	assert(query_calls == 1 && resolve_calls == 1);
	assert(transport.base == 0x400000 && transport.size == 0x1000);
	assert(ops.complete(ops.context, &grant) == CB_SUCCESS);
	assert(install_calls == 1 && close_calls == 0);
	assert(starbook_mtl_mor_private_boundary_secrets_zero_test());
	assert(ops.close(ops.context) == CB_ERR);
}

static void test_close_paths(void)
{
	struct starbook_mtl_mor_private_boundary_ops ops;
	uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE];

	reset();
	assert(starbook_mtl_mor_private_boundary(&ops));
	assert(ops.close(ops.context) == CB_SUCCESS);
	assert(!close_calls);

	reset();
	prepare(&ops, owner);
	assert(ops.close(ops.context) == CB_SUCCESS);
	assert(!close_calls);

	reset();
	prepare(&ops, owner);
	publish(&ops);
	assert(ops.close(ops.context) == CB_SUCCESS);
	assert(close_calls == 1);
	assert(ops.close(ops.context) == CB_ERR);
}

static void test_fail_closed(void)
{
	struct starbook_mtl_mor_private_boundary_ops ops;
	struct bootmem_aligned_reservation transport;
	struct bootmem_aligned_reservation original;
	struct payload_mm_authvar_mor_private_smi_seed duplicate;
	struct payload_mm_authvar_mor_private_smi_seed duplicate_original;
	struct payload_mm_authvar_mor_grant grant = { 0 };
	uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE];

	reset();
	random_fail_at = 2;
	memset(owner, 0xa5, sizeof(owner));
	assert(starbook_mtl_mor_private_boundary(&ops));
	assert(ops.arena_seed(ops.context, 7, owner) == CB_ERR);
	assert(starbook_mtl_mor_private_boundary_secrets_zero_test());

	reset();
	prepare(&ops, owner);
	publish(&ops);
	memset(&duplicate, 0x3c, sizeof(duplicate));
	duplicate_original = duplicate;
	assert(!platform_payload_mm_authvar_mor_private_smi_seed(&duplicate));
	assert(!memcmp(&duplicate, &duplicate_original, sizeof(duplicate)));
	assert(ops.close(ops.context) == CB_SUCCESS);
	assert(close_calls == 1);

	reset();
	prepare(&ops, owner);
	publish(&ops);
	bad_query = true;
	memset(&transport, 0xa5, sizeof(transport));
	original = transport;
	assert(ops.resolve(ops.context, 7, owner, &transport) == CB_ERR);
	assert(!memcmp(&transport, &original, sizeof(transport)));
	assert(ops.close(ops.context) == CB_SUCCESS);
	assert(close_calls == 1);

	reset();
	prepare(&ops, owner);
	publish(&ops);
	bad_channel = true;
	memset(&transport, 0x5a, sizeof(transport));
	original = transport;
	assert(ops.resolve(ops.context, 7, owner, &transport) == CB_ERR);
	assert(!memcmp(&transport, &original, sizeof(transport)));
	assert(ops.close(ops.context) == CB_SUCCESS);
	assert(close_calls == 1);

	reset();
	prepare(&ops, owner);
	publish(&ops);
	query_above_limit = true;
	assert(ops.resolve(ops.context, 7, owner, &transport) == CB_ERR);
	assert(ops.close(ops.context) == CB_SUCCESS);
	assert(close_calls == 1);

	reset();
	prepare(&ops, owner);
	publish(&ops);
	assert(ops.resolve(ops.context, 7, owner, &transport) == CB_SUCCESS);
	install_status = CB_ERR;
	assert(ops.complete(ops.context, &grant) == CB_ERR);
	assert(install_calls == 1 && close_calls == 0);
	assert(starbook_mtl_mor_private_boundary_secrets_zero_test());
	assert(ops.close(ops.context) == CB_ERR);
}

static void test_private_state_overlap(void)
{
	struct starbook_mtl_mor_private_boundary_ops ops;
	struct bootmem_aligned_reservation transport = { 0 };
	uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE];

	reset();
	assert(!starbook_mtl_mor_private_boundary(
		(void *)&private_boundary));
	assert(starbook_mtl_mor_private_boundary_secrets_zero_test());

	reset();
	prepare(&ops, owner);
	assert(ops.reservations_register(ops.context, 7) == CB_SUCCESS);
	assert(!platform_payload_mm_authvar_mor_private_smi_seed(
		(void *)&private_boundary));
	assert(starbook_mtl_mor_private_boundary_secrets_zero_test());
	assert(ops.close(ops.context) == CB_SUCCESS);

	reset();
	prepare(&ops, owner);
	publish(&ops);
	assert(ops.resolve(ops.context, 7, private_boundary.owner,
		&transport) == CB_ERR);
	assert(ops.close(ops.context) == CB_SUCCESS);
	assert(close_calls == 1);

	reset();
	prepare(&ops, owner);
	publish(&ops);
	assert(ops.resolve(ops.context, 7, owner, &transport) == CB_SUCCESS);
	assert(ops.complete(ops.context, (void *)&private_boundary) == CB_ERR);
	assert(ops.close(ops.context) == CB_SUCCESS);
	assert(close_calls == 1);
}

static void test_resolve_input_output_overlap(void)
{
	union {
		struct bootmem_aligned_reservation transport_alignment;
		uint8_t bytes[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE];
	} shared;
	struct starbook_mtl_mor_private_boundary_ops ops;
	uint8_t original[sizeof(shared.bytes)];

	reset();
	memset(shared.bytes, 0xa5, sizeof(shared.bytes));
	assert(starbook_mtl_mor_private_boundary(&ops));
	assert(ops.arena_seed(ops.context, 7, shared.bytes) == CB_SUCCESS);
	publish(&ops);
	memcpy(original, shared.bytes, sizeof(original));
	assert(ops.resolve(ops.context, 7, shared.bytes,
		(void *)shared.bytes) == CB_ERR);
	assert(!memcmp(shared.bytes, original, sizeof(original)));
	assert(ops.close(ops.context) == CB_SUCCESS && close_calls == 1);
}

static void test_completion_close_ordering(void)
{
	struct starbook_mtl_mor_private_boundary_ops ops;
	struct bootmem_aligned_reservation transport = { 0 };
	struct payload_mm_authvar_mor_grant grant = { 0 };
	uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE];

	reset();
	prepare(&ops, owner);
	publish(&ops);
	assert(ops.complete(ops.context, &grant) == CB_ERR);
	assert(!private_boundary.close_claimed && private_boundary.channel_open);
	assert(ops.close(ops.context) == CB_SUCCESS);
	assert(close_calls == 1);

	reset();
	prepare(&ops, owner);
	publish(&ops);
	__atomic_store_n(&private_boundary.phase, MTL_MOR_PRIVATE_RESOLVING,
		__ATOMIC_RELEASE);
	assert(ops.close(ops.context) == CB_ERR);
	assert(!private_boundary.close_claimed && private_boundary.channel_open);
	__atomic_store_n(&private_boundary.phase, MTL_MOR_PRIVATE_PUBLISHED,
		__ATOMIC_RELEASE);
	assert(ops.close(ops.context) == CB_SUCCESS);
	assert(close_calls == 1);

	reset();
	prepare(&ops, owner);
	publish(&ops);
	assert(ops.resolve(ops.context, 7, owner, &transport) == CB_SUCCESS);
	assert(ops.complete(ops.context, &grant) == CB_SUCCESS);
	assert(ops.complete(ops.context, &grant) == CB_ERR);
	assert(install_calls == 1 && !private_boundary.channel_open);
	assert(ops.close(ops.context) == CB_ERR);
}

static void test_concurrent_close(void)
{
	struct starbook_mtl_mor_private_boundary_ops ops;
	struct close_call calls[2];
	pthread_t threads[2];
	uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE];

	reset();
	prepare(&ops, owner);
	publish(&ops);
	for (size_t index = 0; index < ARRAY_SIZE(calls); index++)
		calls[index].ops = &ops;
	assert(!pthread_create(&threads[0], NULL, concurrent_close, &calls[0]));
	assert(!pthread_create(&threads[1], NULL, concurrent_close, &calls[1]));
	assert(!pthread_join(threads[0], NULL));
	assert(!pthread_join(threads[1], NULL));
	assert((calls[0].status == CB_SUCCESS) !=
		(calls[1].status == CB_SUCCESS));
	assert(close_calls == 1 && !private_boundary.channel_open);
}

int main(void)
{
	test_install();
	test_close_paths();
	test_fail_closed();
	test_private_state_overlap();
	test_resolve_input_output_overlap();
	test_completion_close_ordering();
	test_concurrent_close();
	return 0;
}
