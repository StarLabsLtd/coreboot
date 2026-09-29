/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#include <bootmem.h>
#include <bootmem_reservation_receipt.h>
#include <random.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static uint8_t page[4096] __aligned(4096);
static struct bootmem_aligned_reservation_handle registered_handle;
static unsigned int random_calls;
static unsigned int provision_calls;
static unsigned int emit_calls;
static bool random_ok;
static bool provision_ok;
static bool emit_ok;
static uint32_t provisioned_boot_kind;
enum receipt_mutation {
	MUTATE_NONE, MUTATE_REVISION, MUTATE_SIZE, MUTATE_BOOT_KIND,
	MUTATE_RESERVED, MUTATE_SEQUENCE, MUTATE_GENERATION, MUTATE_HANDLE,
	MUTATE_BASE, MUTATE_BYTES, MUTATE_TAG, MUTATE_USE, MUTATE_COUNT,
};
static enum receipt_mutation receipt_mutation;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_endpoint_validate(
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint)
{
	return endpoint ? CB_SUCCESS : CB_ERR;
}

enum cb_err get_random_number_64(uint64_t *value)
{
	random_calls++;
	if (!random_ok)
		return CB_ERR;
	*value = 0x8000000000000000ULL | random_calls;
	return CB_SUCCESS;
}

int bootmem_aligned_reservation_register(
	const struct bootmem_aligned_reservation_request *request,
	struct bootmem_aligned_reservation_handle *handle)
{
	assert(request->bytes == sizeof(page));
	assert(request->alignment == sizeof(page));
	assert(request->tag == BM_MEM_RESERVED);
	registered_handle.opaque[0] = 0x1234;
	*handle = registered_handle;
	return 0;
}

int bootmem_aligned_reservation_query(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_aligned_reservation *reservation)
{
	assert(!memcmp(handle, &registered_handle, sizeof(*handle)));
	*reservation = (struct bootmem_aligned_reservation) {
		.base = (uintptr_t)page,
		.size = sizeof(page),
		.tag = BM_MEM_RESERVED,
	};
	return 0;
}

enum cb_err bootmem_reservation_receipt_provision(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier,
	uint8_t secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE],
	uint32_t boot_kind, uint64_t generation,
	const struct bootmem_aligned_reservation_handle *handle)
{
	provision_calls++;
	assert(boot_kind == BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT);
	assert(generation);
	assert(!memcmp(handle, &registered_handle, sizeof(*handle)));
	if (!provision_ok)
		return CB_ERR;
	memset(signer, 0x5a, sizeof(*signer));
	memset(verifier, 0xa5, sizeof(*verifier));
	signer->generation = generation;
	verifier->generation = generation;
	provisioned_boot_kind = boot_kind;
	memset(secret, 0, BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE);
	return CB_SUCCESS;
}

enum cb_err bootmem_aligned_reservation_receipt_emit_exact_tag(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt *receipt,
	enum bootmem_type expected_tag)
{
	emit_calls++;
	assert(!memcmp(handle, &registered_handle, sizeof(*handle)));
	if (!emit_ok)
		return CB_ERR;
	*receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(*receipt),
		.boot_kind = provisioned_boot_kind,
		.generation = signer->generation,
		.sequence = 1,
		.handle = *handle,
		.base = (uintptr_t)page,
		.bytes = sizeof(page),
		.tag = expected_tag,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	switch (receipt_mutation) {
	case MUTATE_REVISION:
		receipt->revision++;
		break;
	case MUTATE_SIZE:
		receipt->size++;
		break;
	case MUTATE_BOOT_KIND:
		receipt->boot_kind++;
		break;
	case MUTATE_RESERVED:
		receipt->reserved++;
		break;
	case MUTATE_SEQUENCE:
		receipt->sequence = 0;
		break;
	case MUTATE_GENERATION:
		receipt->generation++;
		break;
	case MUTATE_HANDLE:
		receipt->handle.opaque[0]++;
		break;
	case MUTATE_BASE:
		receipt->base++;
		break;
	case MUTATE_BYTES:
		receipt->bytes++;
		break;
	case MUTATE_TAG:
		receipt->tag = BM_MEM_RAM;
		break;
	case MUTATE_USE:
		receipt->use++;
		break;
	case MUTATE_NONE:
	case MUTATE_COUNT:
		break;
	}
	memset(signer, 0, sizeof(*signer));
	return CB_SUCCESS;
}

#include "../../src/lib/payload_mm_authvar_presence_lifecycle_close_backing.c"

static void fixture(void)
{
	memset(&owner, 0, sizeof(owner));
	memset(&registered_handle, 0, sizeof(registered_handle));
	memset(page, 0xcc, sizeof(page));
	random_calls = 0;
	provision_calls = 0;
	emit_calls = 0;
	random_ok = true;
	provision_ok = true;
	emit_ok = true;
	provisioned_boot_kind = 0;
	receipt_mutation = MUTATE_NONE;
}

static bool all_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

static void test_verifier_moves_once_before_backing_take(void)
{
	struct bootmem_reservation_receipt_authority destination = { 0 };
	struct bootmem_reservation_receipt_authority second = { 0 };
	struct payload_mm_authvar_presence_lifecycle_close_backing backing;
	struct bootmem_reservation_receipt receipt;

	fixture();
	assert(payload_mm_authvar_presence_lifecycle_close_backing_reserve() ==
		CB_SUCCESS);
	assert(provision_calls == 1);
	assert(payload_mm_authvar_presence_lifecycle_close_backing_take(&backing) ==
		CB_ERR);
	assert(owner.state == BACKING_FAILED);

	fixture();
	assert(payload_mm_authvar_presence_lifecycle_close_backing_reserve() ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_close_backing_verifier_take(
		&destination));
	assert(!all_zero(&destination, sizeof(destination)));
	assert(all_zero(&owner.verifier, sizeof(owner.verifier)));
	assert(!payload_mm_authvar_presence_lifecycle_close_backing_verifier_take(
		&second));
	assert(payload_mm_authvar_presence_lifecycle_close_backing_take(&backing) ==
		CB_SUCCESS);
	assert(backing.base == (uintptr_t)page && backing.bytes == sizeof(page));
	assert(all_zero(page, sizeof(page)));
	assert(payload_mm_authvar_presence_lifecycle_close_backing_attest(&receipt) ==
		CB_SUCCESS);
	assert(emit_calls == 1 && receipt.base == backing.base &&
		receipt.bytes == backing.bytes && receipt.tag == BM_MEM_RESERVED &&
		receipt.use == BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE);
	assert(owner.state == BACKING_PROVIDER);
}

static void test_failure_is_terminal_and_scrubs(void)
{
	struct bootmem_reservation_receipt_authority destination = { 0 };
	struct payload_mm_authvar_presence_lifecycle_close_backing backing;
	struct bootmem_reservation_receipt receipt;

	fixture();
	random_ok = false;
	assert(payload_mm_authvar_presence_lifecycle_close_backing_reserve() == CB_ERR);
	assert(owner.state == BACKING_FAILED);
	assert(payload_mm_authvar_presence_lifecycle_close_backing_reserve() == CB_ERR);

	fixture();
	provision_ok = false;
	assert(payload_mm_authvar_presence_lifecycle_close_backing_reserve() == CB_ERR);
	assert(owner.state == BACKING_FAILED);
	assert(all_zero(&owner.signer, sizeof(owner.signer)));
	assert(all_zero(&owner.verifier, sizeof(owner.verifier)));

	fixture();
	assert(payload_mm_authvar_presence_lifecycle_close_backing_reserve() ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_close_backing_verifier_take(
		&destination));
	assert(payload_mm_authvar_presence_lifecycle_close_backing_take(&backing) ==
		CB_SUCCESS);
	memset(page, 0xcc, sizeof(page));
	emit_ok = false;
	assert(payload_mm_authvar_presence_lifecycle_close_backing_attest(&receipt) ==
		CB_ERR);
	assert(owner.state == BACKING_FAILED);
	assert(all_zero(page, sizeof(page)));
	assert(all_zero(&receipt, sizeof(receipt)));
	assert(all_zero(&owner.signer, sizeof(owner.signer)));
	assert(all_zero(&owner.verifier, sizeof(owner.verifier)));
}

static void test_destination_rules(void)
{
	uint8_t bytes[sizeof(struct bootmem_reservation_receipt_authority) + 8]
		__aligned(8) = { 0 };
	struct bootmem_reservation_receipt_authority *destination = (void *)bytes;

	fixture();
	assert(payload_mm_authvar_presence_lifecycle_close_backing_reserve() ==
		CB_SUCCESS);
	assert(!payload_mm_authvar_presence_lifecycle_close_backing_verifier_take(
		(void *)(bytes + 1)));
	destination->generation = 1;
	assert(!payload_mm_authvar_presence_lifecycle_close_backing_verifier_take(
		destination));
	memset(destination, 0, sizeof(*destination));
	assert(payload_mm_authvar_presence_lifecycle_close_backing_verifier_take(
		destination));
}

static void test_attestation_mutations_fail_terminally(void)
{
	for (enum receipt_mutation mutation = MUTATE_REVISION;
	     mutation < MUTATE_COUNT; mutation++) {
		struct bootmem_reservation_receipt_authority destination = { 0 };
		struct payload_mm_authvar_presence_lifecycle_close_backing backing;
		struct bootmem_reservation_receipt receipt;

		fixture();
		receipt_mutation = mutation;
		assert(payload_mm_authvar_presence_lifecycle_close_backing_reserve() ==
			CB_SUCCESS);
		assert(payload_mm_authvar_presence_lifecycle_close_backing_verifier_take(
			&destination));
		assert(payload_mm_authvar_presence_lifecycle_close_backing_take(&backing) ==
			CB_SUCCESS);
		memset(page, 0xcc, sizeof(page));
		assert(payload_mm_authvar_presence_lifecycle_close_backing_attest(
			&receipt) == CB_ERR);
		assert(owner.state == BACKING_FAILED);
		assert(all_zero(page, sizeof(page)));
		assert(all_zero(&receipt, sizeof(receipt)));
		assert(all_zero(&owner.signer, sizeof(owner.signer)));
	}
}

int main(void)
{
	test_verifier_moves_once_before_backing_take();
	test_failure_is_terminal_and_scrubs();
	test_destination_rules();
	test_attestation_mutations_fail_terminally();
	return 0;
}
