/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_boot_private_buffer.h>

/* Reuse the real bootmem maps/resource fixture, not a substitute allocator. */
#define main existing_bootmem_fixture_main
#include "bootmem_aligned_reservation_test.c"
#undef main

static void authority_scrubbed(const struct bootmem_reservation_receipt_authority *authority)
{
	struct bootmem_reservation_receipt_authority copy = *authority;

	CHECK(copy.state == 4);
	copy.state = 0;
	CHECK(!memcmp(&copy, (uint8_t[sizeof(copy)]) { 0 }, sizeof(copy)));
}

int main(int argc, char **argv)
{
	struct bootmem_aligned_reservation_handle handle;
	struct bootmem_reservation_receipt_authority signer = { 0 }, verifier = { 0 };
	struct bootmem_reservation_receipt receipt;
	struct lb_payload_boot_private_buffer record;
	uint8_t secret[32];
	const uint8_t literal[40] = {
		[0] = 0x58, [4] = 40, [8] = 1, [10] = 40,
		[14] = 1, [18] = 0x0d, [19] = 2, [26] = 3, [28] = 3,
	};
	bool wrong_shape, wrong_tag;
	unsigned int mutation = 0;

	CHECK(argc == 2);
	if (!strcmp(argv[1], "invalid-arguments")) {
		CHECK(payload_boot_private_buffer_reserve(NULL));
		CHECK(payload_boot_private_buffer_reserve((void *)(UINTPTR_MAX - 3U)));
		CHECK(payload_boot_private_buffer_emit(NULL, &signer, &receipt) == CB_ERR);
		CHECK(payload_boot_private_buffer_emit(&handle, &signer,
			(void *)&signer) == CB_ERR);
		CHECK(payload_boot_private_buffer_consume(&verifier, &receipt,
			(void *)(UINTPTR_MAX - 16U)) == CB_ERR);
		CHECK(payload_boot_private_buffer_consume(&verifier, &receipt,
			(void *)&verifier) == CB_ERR);
		CHECK(payload_boot_private_buffer_consume(&verifier, &receipt,
			(void *)&receipt) == CB_ERR);
		return 0;
	}
	wrong_shape = strstr(argv[1], "wrong-shape") != NULL;
	wrong_tag = strstr(argv[1], "wrong-tag") != NULL;
	if (wrong_shape || wrong_tag) {
		struct bootmem_aligned_reservation_request value = request(
			wrong_shape ? 65536U : 196608U, 4096U, 1ULL << 32,
			wrong_tag ? BM_MEM_TABLE : BM_MEM_RESERVED);

		CHECK(!bootmem_aligned_reservation_register(&value, &handle));
	} else {
		CHECK(!payload_boot_private_buffer_reserve(&handle));
	}
	memset(secret, 0x5a, sizeof(secret));
	CHECK(bootmem_reservation_receipt_provision(&signer, &verifier, secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, 7, &handle) == CB_SUCCESS);
	CHECK(!memcmp(secret, (uint8_t[32]) { 0 }, sizeof(secret)));
	memset(&receipt, 0xa5, sizeof(receipt));
	memset(&record, 0xa5, sizeof(record));
	if (!strcmp(argv[1], "before-map")) {
		CHECK(payload_boot_private_buffer_emit(&handle, &signer, &receipt) == CB_ERR);
		CHECK(!memcmp(&receipt, (uint8_t[sizeof(receipt)]) { 0 }, sizeof(receipt)));
		CHECK(payload_boot_private_buffer_consume(&verifier, &receipt, &record) == CB_ERR);
		CHECK(!memcmp(&record, (uint8_t[40]) { 0 }, sizeof(record)));
		return 0;
	}
	initialize();
	if (wrong_shape || wrong_tag) {
		if (!strncmp(argv[1], "signed-", 7)) {
			/* A real signed reservation with wrong shape/tag is still refused. */
			CHECK(bootmem_aligned_reservation_receipt_emit_exact_tag(&handle, &signer,
				&receipt, wrong_tag ? BM_MEM_TABLE : BM_MEM_RESERVED) == CB_SUCCESS);
		} else {
			CHECK(payload_boot_private_buffer_emit(&handle, &signer, &receipt) == CB_ERR);
			CHECK(!memcmp(&receipt, (uint8_t[sizeof(receipt)]) { 0 }, sizeof(receipt)));
		}
		CHECK(payload_boot_private_buffer_consume(&verifier, &receipt, &record) == CB_ERR);
		CHECK(!memcmp(&record, (uint8_t[40]) { 0 }, sizeof(record)));
		return 0;
	}
	CHECK(payload_boot_private_buffer_emit(&handle, &signer, &receipt) == CB_SUCCESS);
	CHECK(receipt.base == 0x20d0000 && receipt.bytes == 196608U);
	CHECK(os_range_exact(receipt.base, receipt.bytes, LB_MEM_RESERVED));
	if (!strncmp(argv[1], "mutation-", 9)) {
		const char *cursor = argv[1] + 9;

		CHECK(*cursor);
		for (; *cursor; cursor++) {
			CHECK(*cursor >= '0' && *cursor <= '9');
			mutation = mutation * 10U + (unsigned int)(*cursor - '0');
			CHECK(mutation < sizeof(receipt));
		}
		CHECK(mutation < sizeof(receipt));
		((uint8_t *)&receipt)[mutation] ^= 0x80;
		CHECK(payload_boot_private_buffer_consume(&verifier, &receipt, &record) == CB_ERR);
		CHECK(!memcmp(&record, (uint8_t[40]) { 0 }, sizeof(record)));
		CHECK(!memcmp(&receipt, (uint8_t[sizeof(receipt)]) { 0 }, sizeof(receipt)));
		authority_scrubbed(&verifier);
		return 0;
	}
	CHECK(!strcmp(argv[1], "success"));
	CHECK(payload_boot_private_buffer_consume(&verifier, &receipt, &record) == CB_SUCCESS);
	CHECK(!memcmp(&record, literal, sizeof(literal)));
	CHECK(!memcmp(&receipt, (uint8_t[sizeof(receipt)]) { 0 }, sizeof(receipt)));
	authority_scrubbed(&verifier);
	memset(&record, 0xa5, sizeof(record));
	CHECK(payload_boot_private_buffer_consume(&verifier, &receipt, &record) == CB_ERR);
	CHECK(!memcmp(&record, (uint8_t[40]) { 0 }, sizeof(record)));
	memset(&handle, 0xa5, sizeof(handle));
	CHECK(payload_boot_private_buffer_reserve(&handle));
	CHECK(!memcmp(&handle, (uint8_t[sizeof(handle)]) { 0 }, sizeof(handle)));
	return 0;
}
