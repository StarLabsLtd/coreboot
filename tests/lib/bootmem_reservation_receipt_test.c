/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../src/lib/bootmem_reservation_receipt_internal.h"
#include <string.h>

#define CHECK(x) do { if (!(x)) __builtin_trap(); } while (0)

static const uint8_t test_key[32] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
static int hook_mode;
static bool signer_was_claimable;

static bool zero(const void *p, size_t n)
{
	const uint8_t *b = p;
	uint8_t v = 0;

	while (n)
		v |= b[--n];
	return !v;
}

static bool terminal_and_scrubbed(
	const struct bootmem_reservation_receipt_authority *authority)
{
	struct bootmem_reservation_receipt_authority copy = *authority;

	CHECK(copy.state == 4);
	copy.state = 0;
	return zero(&copy, sizeof(copy));
}

static bool hex_equal(const uint8_t digest[32], const char *hex)
{
	static const char digits[] = "0123456789abcdef";

	for (size_t i = 0; i < 32; i++) {
		if (hex[2 * i] != digits[digest[i] >> 4] ||
		    hex[2 * i + 1] != digits[digest[i] & 15])
			return false;
	}
	return hex[64] == '\0';
}

void bootmem_receipt_test_after_claim_cas(
	struct bootmem_reservation_receipt_authority *authority)
{
	if (hook_mode == 7)
		bootmem_reservation_receipt_close(authority);
}

void bootmem_receipt_test_after_claim(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier)
{
	(void)verifier;
	if (hook_mode == 5) {
		bootmem_reservation_receipt_close(signer);
		bootmem_reservation_receipt_close(signer);
	}
}

void bootmem_receipt_test_after_signer_fill(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier)
{
	(void)signer;
	if (hook_mode == 6) {
		bootmem_reservation_receipt_close(verifier);
		bootmem_reservation_receipt_close(verifier);
	}
}

void bootmem_receipt_test_after_fill(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier)
{
	if (hook_mode == 1)
		bootmem_reservation_receipt_close(signer);
	else if (hook_mode == 2)
		bootmem_reservation_receipt_close(verifier);
}

void bootmem_receipt_test_after_verifier_publish(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier)
{
	struct bootmem_reservation_receipt_authority snapshot = {0};

	if (hook_mode == 3)
		bootmem_reservation_receipt_close(verifier);
	else if (hook_mode == 4)
		signer_was_claimable =
			bootmem_reservation_receipt_authority_claim(signer, &snapshot);
	bootmem_reservation_receipt_scrub(&snapshot, sizeof(snapshot));
}

static enum cb_err provision(struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier, uint8_t key[32],
	uint32_t boot_kind)
{
	const struct bootmem_aligned_reservation_handle handle = {
		.opaque = {3, 0x42524d51},
	};

	memcpy(key, test_key, sizeof(test_key));
	return bootmem_reservation_receipt_provision(signer, verifier, key,
		boot_kind, 0x1122334455667788ULL, &handle);
}

static void make_tag(struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt, enum bootmem_type tag)
{
	uint8_t key[32];

	CHECK(provision(signer, verifier, key,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT) == CB_SUCCESS);
	CHECK(zero(key, sizeof(key)));
	*receipt = (struct bootmem_reservation_receipt) {
		.revision = 1,
		.size = sizeof(*receipt),
		.boot_kind = 1,
		.generation = 0x1122334455667788ULL,
		.sequence = 1,
		.handle = {.opaque = {3, 0x42524d51}},
		.base = 0x180000,
		.bytes = 0x1000,
		.tag = tag,
		.use = 1,
	};
	CHECK(bootmem_reservation_receipt_mac(test_key, receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt->mac) ==
		CB_SUCCESS);
}

static void make(struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt)
{
	make_tag(signer, verifier, receipt, BM_MEM_TABLE);
}

static void check_kats(void)
{
	static const struct {
		size_t size;
		const char *mac;
	} boundary[] = {
		{55, "dfa116fb2a8a9d0b01ad624cde83816b5d300490d1b54335d27509384fd523dc"},
		{56, "509fa91fee82bec7d460087685bb4e9e3bfc1fa9b3b92946f0f0d54769a89362"},
		{63, "8312caefd346f01221f839c2bd5055af17d18530aad482c20a16f46183774222"},
		{64, "099e45e9f7e98202464efb532e5230a0a0e946780ef355b3b9d064954935a6ee"},
		{65, "96bd4474eea0f1b3eaf658454c4b1f0635ba46995db417a5d7159b867fa4dee2"},
	};
	uint8_t message[65] = {0};
	uint8_t prefix[64];
	uint8_t mac[32];

	for (size_t i = 0; i < 64; i++)
		prefix[i] = i;
	CHECK(bootmem_reservation_receipt_mac(test_key, prefix, sizeof(prefix),
		mac) == CB_SUCCESS);
	CHECK(hex_equal(mac,
		"173206781c3b828a0dc2a716fe0ddb5e6e56ec171170952ff6b3f4de44fa18d7"));
	for (size_t i = 0; i < ARRAY_SIZE(boundary); i++) {
		CHECK(bootmem_reservation_receipt_mac(test_key, message,
			boundary[i].size, mac) == CB_SUCCESS);
		CHECK(hex_equal(mac, boundary[i].mac));
	}
}

static void check_reentry(int mode)
{
	struct bootmem_reservation_receipt_authority signer = {0}, verifier = {0};
	uint8_t key[32];

	hook_mode = mode;
	signer_was_claimable = false;
	if (mode == 4) {
		CHECK(provision(&signer, &verifier, key, 1) == CB_SUCCESS);
		CHECK(!signer_was_claimable);
		bootmem_reservation_receipt_close(&signer);
		bootmem_reservation_receipt_close(&verifier);
	} else {
		CHECK(provision(&signer, &verifier, key, 1) != CB_SUCCESS);
		CHECK(terminal_and_scrubbed(&signer));
		CHECK(terminal_and_scrubbed(&verifier));
	}
	CHECK(zero(key, sizeof(key)));
	hook_mode = 0;
}

static void check_claim_close(void)
{
	struct bootmem_reservation_receipt_authority signer = {0}, verifier = {0};
	struct bootmem_reservation_receipt_authority snapshot = {0};
	uint8_t key[32];

	CHECK(provision(&signer, &verifier, key, 1) == CB_SUCCESS);
	hook_mode = 7;
	CHECK(bootmem_reservation_receipt_authority_claim(&signer, &snapshot));
	hook_mode = 0;
	CHECK(!memcmp(snapshot.secret, test_key, sizeof(test_key)));
	CHECK(terminal_and_scrubbed(&signer));
	bootmem_reservation_receipt_close(&snapshot);
	bootmem_reservation_receipt_close(&verifier);
	CHECK(terminal_and_scrubbed(&snapshot));
	CHECK(terminal_and_scrubbed(&verifier));
}

int main(int argc, char **argv)
{
	struct bootmem_reservation_receipt_authority signer = {0}, verifier = {0};
	struct bootmem_reservation_receipt receipt = {0}, saved;
	uint8_t key[32];

	CHECK(argc == 2);
	if (!strcmp(argv[1], "kat")) {
		check_kats();
		return 0;
	}
	if (!strcmp(argv[1], "abort-signer")) {
		check_reentry(1);
		return 0;
	}
	if (!strcmp(argv[1], "abort-verifier")) {
		check_reentry(2);
		return 0;
	}
	if (!strcmp(argv[1], "close-ready-verifier")) {
		check_reentry(3);
		return 0;
	}
	if (!strcmp(argv[1], "ordering")) {
		check_reentry(4);
		return 0;
	}
	if (!strcmp(argv[1], "double-close-before-fill")) {
		check_reentry(5);
		return 0;
	}
	if (!strcmp(argv[1], "double-close-during-fill")) {
		check_reentry(6);
		return 0;
	}
	if (!strcmp(argv[1], "claim-close")) {
		check_claim_close();
		return 0;
	}
	if (!strcmp(argv[1], "s3") || !strcmp(argv[1], "alias")) {
		struct bootmem_aligned_reservation_handle handle = {.opaque = {1, 2}};

		memcpy(key, test_key, sizeof(key));
		CHECK(bootmem_reservation_receipt_provision(&signer,
			!strcmp(argv[1], "alias") ? &signer : &verifier, key,
			!strcmp(argv[1], "s3") ? 2 : 1, 1, &handle) != CB_SUCCESS);
		CHECK(zero(key, sizeof(key)));
		CHECK(terminal_and_scrubbed(&signer));
		if (strcmp(argv[1], "alias"))
			CHECK(terminal_and_scrubbed(&verifier));
		return 0;
	}
	make(&signer, &verifier, &receipt);
	if (!strcmp(argv[1], "success"))
		CHECK(bootmem_reservation_receipt_verify_consume(&verifier,
			&receipt) == CB_SUCCESS);
	else if (!strcmp(argv[1], "exact-table"))
		CHECK(bootmem_reservation_receipt_verify_consume_exact_tag(&verifier,
			&receipt, BM_MEM_TABLE) == CB_SUCCESS);
	else if (!strcmp(argv[1], "exact-reserved")) {
		bootmem_reservation_receipt_close(&signer);
		bootmem_reservation_receipt_close(&verifier);
		memset(&signer, 0, sizeof(signer));
		memset(&verifier, 0, sizeof(verifier));
		make_tag(&signer, &verifier, &receipt, BM_MEM_RESERVED);
		CHECK(bootmem_reservation_receipt_verify_consume_exact_tag(&verifier,
			&receipt, BM_MEM_RESERVED) == CB_SUCCESS);
		CHECK(zero(&receipt, sizeof(receipt)));
	} else if (!strcmp(argv[1], "exact-wrong-tag")) {
		CHECK(bootmem_reservation_receipt_verify_consume_exact_tag(&verifier,
			&receipt, BM_MEM_RESERVED) != CB_SUCCESS);
		CHECK(zero(&receipt, sizeof(receipt)));
		CHECK(terminal_and_scrubbed(&verifier));
	} else if (!strcmp(argv[1], "exact-unsupported-tag")) {
		CHECK(bootmem_reservation_receipt_verify_consume_exact_tag(&verifier,
			&receipt, BM_MEM_RAM) != CB_SUCCESS);
		CHECK(zero(&receipt, sizeof(receipt)));
		CHECK(terminal_and_scrubbed(&verifier));
	} else if (!strcmp(argv[1], "exact-replay")) {
		saved = receipt;
		CHECK(bootmem_reservation_receipt_verify_consume_exact_tag(&verifier,
			&receipt, BM_MEM_TABLE) == CB_SUCCESS);
		CHECK(bootmem_reservation_receipt_verify_consume_exact_tag(&verifier,
			&saved, BM_MEM_TABLE) != CB_SUCCESS);
		CHECK(zero(&receipt, sizeof(receipt)));
		CHECK(zero(&saved, sizeof(saved)));
		CHECK(terminal_and_scrubbed(&verifier));
	} else if (!strcmp(argv[1], "replay")) {
		saved = receipt;
		CHECK(bootmem_reservation_receipt_verify_consume(&verifier,
			&receipt) == CB_SUCCESS);
		CHECK(bootmem_reservation_receipt_verify_consume(&verifier,
			&saved) != CB_SUCCESS);
	} else {
		if (!strcmp(argv[1], "stale"))
			receipt.generation++;
		else if (!strcmp(argv[1], "tag")) {
			receipt.tag = BM_MEM_RESERVED;
			CHECK(bootmem_reservation_receipt_mac(test_key, &receipt,
				offsetof(struct bootmem_reservation_receipt, mac),
				receipt.mac) == CB_SUCCESS);
		} else if (!strcmp(argv[1], "mac"))
			receipt.mac[0] ^= 1;
		else if (!strcmp(argv[1], "malformed"))
			receipt.size = 0;
		else
			return 1;
		CHECK(bootmem_reservation_receipt_verify_consume(&verifier,
			&receipt) != CB_SUCCESS);
		CHECK(zero(&receipt, sizeof(receipt)));
	}
	return 0;
}
