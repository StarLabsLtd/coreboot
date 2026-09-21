/* SPDX-License-Identifier: GPL-2.0-only */

#include <lib/payload_mm_fmp_owner_journal_internal.h>
#include <security/tpm/capsule_anchor_platform.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		abort(); \
} while (0)

static struct capsule_tpm_anchor_binding binding(void)
{
	return (struct capsule_tpm_anchor_binding) {
		.policy_revision = CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION,
		.nv_index = 0x01001234,
	};
}

static struct capsule_tpm_anchor_platform_request request(void)
{
	struct capsule_tpm_anchor_platform_request value = {
		.revision = CAPSULE_TPM_ANCHOR_PLATFORM_REQUEST_REVISION,
		.size = sizeof(value),
		.policy_revision = CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION,
		.nv_index = 0x01001234,
		.generation = 0x0102030405060708ULL,
		.transaction = 0x1112131415161718ULL,
		.current.epoch = 7,
		.candidate.epoch = 8,
	};

	memset(value.current.digest, 0x21, sizeof(value.current.digest));
	memset(value.candidate.digest, 0x42, sizeof(value.candidate.digest));
	return value;
}

static struct payload_mm_fmp_owner_platform_receipt receipt(void)
{
	struct payload_mm_fmp_owner_platform_receipt value = {
		.magic = PAYLOAD_MM_FMP_OWNER_PLATFORM_RECEIPT_MAGIC,
		.revision = PAYLOAD_MM_FMP_OWNER_PLATFORM_RECEIPT_REVISION,
		.size = sizeof(value),
		.capsule_size = UINT32_MAX,
		.digest_algorithm = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256,
		.digest_size = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE,
	};

	for (size_t i = 0; i < sizeof(value.capsule_digest); i++)
		value.capsule_digest[i] = (uint8_t)(0x80 + i);
	return value;
}

static void test_request(void)
{
	struct capsule_tpm_anchor_binding expected = binding();
	struct capsule_tpm_anchor_platform_request value = request();

	CHECK(capsule_tpm_anchor_platform_request_valid(&value, &expected));
	value.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION;
	CHECK(!capsule_tpm_anchor_platform_request_valid(&value, &expected));
	value = request();
	value.current.epoch = UINT64_MAX;
	CHECK(!capsule_tpm_anchor_platform_request_valid(&value, &expected));
	value = request();
	value.candidate.epoch++;
	CHECK(!capsule_tpm_anchor_platform_request_valid(&value, &expected));
	value = request();
	memcpy(value.candidate.digest, value.current.digest,
		sizeof(value.current.digest));
	CHECK(!capsule_tpm_anchor_platform_request_valid(&value, &expected));
	expected.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION;
	value = request();
	CHECK(!capsule_tpm_anchor_platform_request_valid(&value, &expected));
}

static void test_receipt(void)
{
	struct payload_mm_fmp_owner_platform_receipt value = receipt();

	CHECK(payload_mm_fmp_owner_platform_receipt_valid(&value));
	value.revision = 1;
	CHECK(!payload_mm_fmp_owner_platform_receipt_valid(&value));
	value = receipt();
	value.capsule_size = 0;
	CHECK(!payload_mm_fmp_owner_platform_receipt_valid(&value));
	value = receipt();
	value.capsule_size = (uint64_t)UINT32_MAX + 1;
	CHECK(!payload_mm_fmp_owner_platform_receipt_valid(&value));
	value = receipt();
	value.digest_algorithm++;
	CHECK(!payload_mm_fmp_owner_platform_receipt_valid(&value));
	value = receipt();
	memset(value.capsule_digest, 0, sizeof(value.capsule_digest));
	CHECK(!payload_mm_fmp_owner_platform_receipt_valid(&value));
}

static void test_transcript(void)
{
	struct payload_mm_fmp_owner_journal_manifest manifest;
	struct payload_mm_fmp_owner_journal_anchor current = {
		.epoch = 0x0807060504030201ULL,
	};
	struct payload_mm_fmp_owner_platform_receipt value = receipt();
	struct payload_mm_fmp_owner_platform_anchor_input input;
	static const uint8_t domain[32] = {
		'P', 'A', 'Y', 'L', 'O', 'A', 'D', '-',
		'M', 'M', '-', 'F', 'M', 'P', '-', 'P',
		'L', 'A', 'T', '-', 'A', 'N', 'C', 'H',
		'O', 'R', '-', 'V', '3', 0, 0, 0,
	};

	memset(&manifest, 0x5a, sizeof(manifest));
	memset(current.digest, 0x33, sizeof(current.digest));
	CHECK(payload_mm_fmp_owner_platform_anchor_input(&manifest, &current,
		0x1817161514131211ULL, 0x2827262524232221ULL, &value,
		&input));
	CHECK(!memcmp(input.bytes, domain, sizeof(domain)));
	CHECK(!memcmp(input.bytes + 32, &manifest, sizeof(manifest)));
	CHECK(!memcmp(input.bytes + 384, current.digest,
		sizeof(current.digest)));
	CHECK(!memcmp(input.bytes + 464, value.capsule_digest,
		sizeof(value.capsule_digest)));
	CHECK(input.bytes[376] == 1 && input.bytes[383] == 8);
	CHECK(input.bytes[416] == 0x11 && input.bytes[423] == 0x18);
	CHECK(input.bytes[424] == 0x21 && input.bytes[431] == 0x28);
	CHECK(input.bytes[432] == 0x50 && input.bytes[439] == 0x33);
	CHECK(input.bytes[440] == 3 && input.bytes[443] == 0);
	CHECK(input.bytes[444] == 64 && input.bytes[447] == 0);
	CHECK(input.bytes[448] == 0xff && input.bytes[451] == 0xff &&
		input.bytes[452] == 0 && input.bytes[455] == 0);
	CHECK(input.bytes[456] == PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256 &&
		input.bytes[460] == PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE);
}

int main(void)
{
	test_request();
	test_receipt();
	test_transcript();
	return 0;
}
