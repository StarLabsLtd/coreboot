/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_grant.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

struct protection_context {
	struct payload_mm_authvar_mor_grant *grant;
	bool protected;
	bool mutate_grant;
	bool mutate_authority;
	bool mutate_candidate;
	const void *authority;
	size_t authority_size;
};

static struct payload_mm_authvar_mor_grant valid_grant(void)
{
	struct payload_mm_authvar_mor_grant grant = {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REVISION,
		.size = sizeof(grant),
		.cold_boot_generation = 7,
		.entry = { .present = 1, .value = 0x11 },
		.flags = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS,
		.dma_policy_generation = 9,
		.inventory_generation = 12,
		.total_bytes = 0x4000,
		.cleared_bytes = 0x3000,
		.excluded_bytes = 0x1000,
		.total_spans = 3,
		.cleared_spans = 2,
		.excluded_spans = 1,
		.spans = {
			{
				.base = 0x1000,
				.size = 0x1000,
				.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
			},
			{
				.base = 0x2000,
				.size = 0x1000,
				.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
				.exclusion_reason =
					PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
			},
			{
				.base = 0x4000,
				.size = 0x2000,
				.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
			},
		},
	};

	for (size_t i = 0; i < sizeof(grant.dma_policy_identity); i++) {
		grant.dma_policy_identity[i] = i + 1;
		grant.inventory_identity[i] = 0x80 + i;
	}
	return grant;
}

static bool protected_storage(void *opaque, const void *storage, size_t size)
{
	struct protection_context *context = opaque;

	CHECK(storage);
	CHECK(size >= sizeof(struct payload_mm_authvar_mor_grant));
	context->authority = storage;
	context->authority_size = size;
	if (context->mutate_grant)
		context->grant->cold_boot_generation++;
	if (context->mutate_authority)
		((uint8_t *)storage)[0] ^= 1;
	if (context->mutate_candidate)
		((uint8_t *)storage)[sizeof(struct payload_mm_authvar_mor_grant)] ^= 1;
	return context->protected;
}

static bool authority_receipts_zero(const struct protection_context *context)
{
	const uint8_t *bytes = context->authority;
	uint8_t combined = 0;

	CHECK(bytes);
	CHECK(context->authority_size >=
		2 * sizeof(struct payload_mm_authvar_mor_grant));
	for (size_t i = 0;
	     i < 2 * sizeof(struct payload_mm_authvar_mor_grant); i++)
		combined |= bytes[i];
	return combined == 0;
}

static void test_validator(void)
{
	struct payload_mm_authvar_mor_grant grant = valid_grant();

	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_SUCCESS);
	grant.entry.value = 0xff;
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_SUCCESS);
	CHECK(payload_mm_authvar_mor_grant_validate(NULL) == CB_ERR_ARG);

#define BAD(field, value) do { \
	grant = valid_grant(); \
	grant.field = (value); \
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR); \
} while (0)
	BAD(revision, 2);
	BAD(size, sizeof(grant) - 1);
	BAD(cold_boot_generation, 0);
	BAD(entry.present, 0);
	BAD(entry.value, 0x10);
	BAD(entry.reserved, 1);
	BAD(flags, PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS & ~(1U << 1));
	BAD(flags, PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS | (1U << 31));
	BAD(dma_policy_generation, 0);
	BAD(inventory_generation, 0);
	BAD(total_bytes, 0);
	BAD(total_bytes, 0x4001);
	BAD(cleared_bytes, 0x3001);
	BAD(excluded_bytes, 0x1001);
	BAD(total_spans, 0);
	BAD(total_spans, PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS + 1);
	BAD(cleared_spans, 1);
	BAD(excluded_spans, 2);
	BAD(reserved, 1);
#undef BAD

	grant = valid_grant();
	memset(grant.dma_policy_identity, 0, sizeof(grant.dma_policy_identity));
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
	grant = valid_grant();
	memset(grant.inventory_identity, 0, sizeof(grant.inventory_identity));
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
	grant = valid_grant();
	grant.spans[0].size = 0;
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
	grant = valid_grant();
	grant.spans[0].base = UINT64_MAX - 7;
	grant.spans[0].size = 8;
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
	grant = valid_grant();
	grant.spans[1].base = 0x1fff;
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
	grant = valid_grant();
	grant.spans[1].span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED;
	grant.spans[1].exclusion_reason = PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_NONE;
	grant.cleared_bytes = 0x4000;
	grant.excluded_bytes = 0;
	grant.cleared_spans = 3;
	grant.excluded_spans = 0;
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
	grant = valid_grant();
	grant.spans[0].span_class = 0;
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
	grant = valid_grant();
	grant.spans[0].exclusion_reason =
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE;
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
	grant = valid_grant();
	grant.spans[1].exclusion_reason = PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_NONE;
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
	grant = valid_grant();
	grant.spans[1].exclusion_reason =
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED + 1;
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
	grant = valid_grant();
	grant.spans[3].base = 1;
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
	grant = valid_grant();
	grant.total_spans = 1;
	grant.cleared_spans = 0;
	grant.excluded_spans = 1;
	grant.cleared_bytes = 0;
	grant.excluded_bytes = grant.total_bytes;
	grant.spans[0].size = grant.total_bytes;
	grant.spans[0].span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED;
	grant.spans[0].exclusion_reason =
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED;
	memset(&grant.spans[1], 0,
		(sizeof(grant.spans) - sizeof(grant.spans[0])));
	CHECK(payload_mm_authvar_mor_grant_validate(&grant) == CB_ERR);
}

static void test_misaligned(void)
{
	uint8_t storage[sizeof(struct payload_mm_authvar_mor_grant) + 8] = { 0 };
	struct payload_mm_authvar_mor_grant *grant =
		(struct payload_mm_authvar_mor_grant *)(storage + 1);
	struct payload_mm_authvar_mor_grant valid = valid_grant();

	memcpy(grant, &valid, sizeof(*grant));
	CHECK(payload_mm_authvar_mor_grant_validate(grant) == CB_ERR_ARG);
}

static void test_success(void)
{
	struct payload_mm_authvar_mor_grant grant = valid_grant();
	struct protection_context context = {
		.grant = &grant,
		.protected = true,
	};

	CHECK(payload_mm_authvar_mor_grant_install(&grant, protected_storage,
		&context) == CB_SUCCESS);
	CHECK(payload_mm_authvar_mor_grant_ready());
	CHECK(payload_mm_authvar_mor_grant_consume(&grant) == CB_SUCCESS);
	CHECK(!payload_mm_authvar_mor_grant_ready());
	CHECK(authority_receipts_zero(&context));
	CHECK(payload_mm_authvar_mor_grant_consume(&grant) == CB_ERR);
	CHECK(payload_mm_authvar_mor_grant_close() == CB_ERR);
}

static void test_close_before_install(void)
{
	struct payload_mm_authvar_mor_grant grant = valid_grant();
	struct protection_context context = {
		.grant = &grant,
		.protected = true,
	};

	CHECK(payload_mm_authvar_mor_grant_close() == CB_SUCCESS);
	CHECK(!payload_mm_authvar_mor_grant_ready());
	CHECK(payload_mm_authvar_mor_grant_close() == CB_ERR);
	CHECK(payload_mm_authvar_mor_grant_install(&grant, protected_storage,
		&context) == CB_ERR);
	CHECK(payload_mm_authvar_mor_grant_close() == CB_ERR);
	CHECK(!payload_mm_authvar_mor_grant_ready());
}

static void test_close_after_install(void)
{
	struct payload_mm_authvar_mor_grant grant = valid_grant();
	struct protection_context context = {
		.grant = &grant,
		.protected = true,
	};

	CHECK(payload_mm_authvar_mor_grant_install(&grant, protected_storage,
		&context) == CB_SUCCESS);
	CHECK(payload_mm_authvar_mor_grant_ready());
	CHECK(payload_mm_authvar_mor_grant_close() == CB_ERR);
	CHECK(payload_mm_authvar_mor_grant_ready());
	CHECK(payload_mm_authvar_mor_grant_consume(&grant) == CB_SUCCESS);
	CHECK(!payload_mm_authvar_mor_grant_ready());
	CHECK(authority_receipts_zero(&context));
	CHECK(payload_mm_authvar_mor_grant_close() == CB_ERR);
}

static void test_consume_mismatch(unsigned int mutation)
{
	struct payload_mm_authvar_mor_grant grant = valid_grant();
	struct payload_mm_authvar_mor_grant expected;
	struct protection_context context = {
		.grant = &grant,
		.protected = true,
	};

	CHECK(payload_mm_authvar_mor_grant_install(&grant, protected_storage,
		&context) == CB_SUCCESS);
	expected = grant;
	switch (mutation) {
	case 0:
		expected.cold_boot_generation++;
		break;
	case 1:
		expected.entry.value ^= 0x10;
		break;
	case 2:
		expected.flags ^= 1U;
		break;
	case 3:
		expected.dma_policy_identity[0] ^= 1U;
		break;
	case 4:
		expected.inventory_identity[0] ^= 1U;
		break;
	case 5:
		expected.spans[0].base++;
		expected.spans[0].size--;
		expected.cleared_bytes--;
		expected.total_bytes--;
		break;
	default:
		CHECK(false);
	}
	CHECK(payload_mm_authvar_mor_grant_consume(&expected) == CB_ERR);
	CHECK(!payload_mm_authvar_mor_grant_ready());
	CHECK(authority_receipts_zero(&context));
}

static void test_consume_null(void)
{
	struct payload_mm_authvar_mor_grant grant = valid_grant();
	struct protection_context context = {
		.grant = &grant,
		.protected = true,
	};

	CHECK(payload_mm_authvar_mor_grant_install(&grant, protected_storage,
		&context) == CB_SUCCESS);
	CHECK(payload_mm_authvar_mor_grant_consume(NULL) == CB_ERR);
	CHECK(!payload_mm_authvar_mor_grant_ready());
	CHECK(authority_receipts_zero(&context));
}

static void test_consume_alias(void)
{
	struct payload_mm_authvar_mor_grant grant = valid_grant();
	struct protection_context context = {
		.grant = &grant,
		.protected = true,
	};

	CHECK(payload_mm_authvar_mor_grant_install(&grant, protected_storage,
		&context) == CB_SUCCESS);
	CHECK(payload_mm_authvar_mor_grant_consume(context.authority) == CB_ERR);
	CHECK(!payload_mm_authvar_mor_grant_ready());
	CHECK(authority_receipts_zero(&context));
}

static void test_install_failure(bool protected, bool mutate_grant,
	bool mutate_authority, bool mutate_candidate, bool malformed)
{
	struct payload_mm_authvar_mor_grant grant = valid_grant();
	struct protection_context context = {
		.grant = &grant,
		.protected = protected,
		.mutate_grant = mutate_grant,
		.mutate_authority = mutate_authority,
		.mutate_candidate = mutate_candidate,
	};

	if (malformed)
		grant.total_bytes++;
	CHECK(payload_mm_authvar_mor_grant_install(&grant, protected_storage,
		&context) == CB_ERR);
	CHECK(!payload_mm_authvar_mor_grant_ready());
	if (context.authority)
		CHECK(authority_receipts_zero(&context));
	CHECK(payload_mm_authvar_mor_grant_install(&grant, protected_storage,
		&context) == CB_ERR);
	CHECK(payload_mm_authvar_mor_grant_close() == CB_ERR);
}

static void test_install_bad_argument(unsigned int kind)
{
	struct payload_mm_authvar_mor_grant valid = valid_grant();
	uint8_t storage[sizeof(valid) + 8] = { 0 };
	struct payload_mm_authvar_mor_grant *grant = &valid;
	payload_mm_authvar_mor_grant_protected_storage callback = protected_storage;
	struct protection_context context = {
		.grant = &valid,
		.protected = true,
	};

	if (kind == 0)
		grant = NULL;
	else if (kind == 1)
		callback = NULL;
	else {
		grant = (struct payload_mm_authvar_mor_grant *)(storage + 1);
		memcpy(grant, &valid, sizeof(*grant));
	}
	CHECK(payload_mm_authvar_mor_grant_install(grant, callback, &context) == CB_ERR);
	CHECK(!payload_mm_authvar_mor_grant_ready());
	CHECK(payload_mm_authvar_mor_grant_install(&valid, protected_storage,
		&context) == CB_ERR);
	CHECK(payload_mm_authvar_mor_grant_close() == CB_ERR);
}

int main(int argc, char **argv)
{
	CHECK(argc == 2);
	if (!strcmp(argv[1], "validator"))
		test_validator();
	else if (!strcmp(argv[1], "misaligned"))
		test_misaligned();
	else if (!strcmp(argv[1], "success"))
		test_success();
	else if (!strcmp(argv[1], "close-before-install"))
		test_close_before_install();
	else if (!strcmp(argv[1], "close-after-install"))
		test_close_after_install();
	else if (!strncmp(argv[1], "consume-mismatch-", 17)) {
		CHECK(argv[1][17] >= '0' && argv[1][17] <= '5' && !argv[1][18]);
		test_consume_mismatch((unsigned int)(argv[1][17] - '0'));
	} else if (!strcmp(argv[1], "consume-null"))
		test_consume_null();
	else if (!strcmp(argv[1], "consume-alias"))
		test_consume_alias();
	else if (!strcmp(argv[1], "install-unprotected"))
		test_install_failure(false, false, false, false, false);
	else if (!strcmp(argv[1], "install-mutate-grant"))
		test_install_failure(true, true, false, false, false);
	else if (!strcmp(argv[1], "install-mutate-authority"))
		test_install_failure(true, false, true, false, false);
	else if (!strcmp(argv[1], "install-mutate-candidate"))
		test_install_failure(true, false, false, true, false);
	else if (!strcmp(argv[1], "install-malformed"))
		test_install_failure(true, false, false, false, true);
	else if (!strncmp(argv[1], "install-bad-", 12)) {
		CHECK(argv[1][12] >= '0' && argv[1][12] <= '2' && !argv[1][13]);
		test_install_bad_argument((unsigned int)(argv[1][12] - '0'));
	} else
		CHECK(false);
	return 0;
}
