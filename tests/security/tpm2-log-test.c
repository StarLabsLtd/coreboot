/* SPDX-License-Identifier: GPL-2.0-only */

#include "../../src/security/tpm/tspi/log-tpm2.c"

#include <stdlib.h>
#include <tests/test.h>

static struct tpm_2_log_table *current_log;

size_t vb2_digest_size(enum vb2_hash_algorithm alg)
{
	return alg == VB2_HASH_SHA256 ? SHA256_DIGEST_SIZE : 0;
}

const char *vb2_get_hash_algorithm_name(enum vb2_hash_algorithm alg)
{
	return "SHA256";
}

void *tpm_log_init(void)
{
	return current_log;
}

static struct tpm_2_log_table *new_log(uint16_t entries, bool fixed)
{
	struct tpm_2_log_table *log = calloc(1, sizeof(*log) +
		entries * sizeof(struct tpm_2_log_entry));

	assert_non_null(log);
	log->vendor.max_entries = htole16(entries);
	log->vendor.entry_size = htole32(fixed ? sizeof(struct tpm_2_log_entry) : 0);
	return log;
}

static void add_entries(struct tpm_2_log_table *log, int entries)
{
	uint8_t digest[SHA256_DIGEST_SIZE] = {};

	for (int i = 0; i < entries; i++)
		assert_true(tpm2_log_append_fixed_event(log, i, EV_ACTION, TPM_MEASURE_ALGO,
						digest, sizeof(digest), "event", 5));
}

static void test_valid_fixed_migration(void **state)
{
	struct tpm_2_log_table *from = new_log(19, true);
	struct tpm_2_log_table *to = new_log(MAX_TPM_LOG_ENTRIES, false);

	add_entries(from, 16);
	assert_int_equal(tpm2_log_copy_entries(from, to), 0);
	assert_int_equal(le16toh(to->vendor.num_entries), 16);
	free(from);
	free(to);
}

static void test_rejects_malformed_fixed_entries(void **state)
{
	struct tpm_2_log_table *from = new_log(3, true);
	int pcr;
	const uint8_t *digest;
	enum vb2_hash_algorithm algo;
	const char *name;

	add_entries(from, 3);
	for (int i = 0; i < 3; i++) {
		struct tpm_2_log_table *to = new_log(MAX_TPM_LOG_ENTRIES, false);

		from->entries[i].data_length = htole32(TPM_20_LOG_DATA_MAX_LENGTH + 1);
		current_log = from;
		assert_int_equal(tpm2_log_get(i, &pcr, &digest, &algo, &name), -1);
		assert_int_equal(tpm2_log_copy_entries(from, to), -1);
		assert_int_equal(le16toh(to->vendor.num_entries), i);
		from->entries[i].data_length = htole32(6);
		free(to);
	}
	free(from);
}

static void test_rejects_persistently_invalid_log(void **state)
{
	struct tpm_2_log_table *log = new_log(1, false);
	int pcr;
	const uint8_t *digest;
	enum vb2_hash_algorithm algo;
	const char *name;

	log->vendor.reserved = 1;
	current_log = log;
	assert_int_equal(tpm2_log_get(0, &pcr, &digest, &algo, &name), -1);
	free(log);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_valid_fixed_migration),
		cmocka_unit_test(test_rejects_malformed_fixed_entries),
		cmocka_unit_test(test_rejects_persistently_invalid_log),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
