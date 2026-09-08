/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <tests/test.h>

static uintptr_t region_base;
static size_t region_size;

void smm_region(uintptr_t *start, size_t *size)
{
	*start = region_base;
	*size = region_size;
}

void __noreturn die(const char *message, ...)
{
	mock_assert(0, message, __FILE__, __LINE__);
	__builtin_unreachable();
}

static void valid_layout(void **state)
{
	static const struct {
		int subregion;
		size_t offset;
		size_t size;
	} expected[] = {
		{ SMM_SUBREGION_HANDLER, 0, 0x5ff000 },
		{ SMM_SUBREGION_PAYLOAD, 0x5ff000, 0x400000 },
		{ SMM_SUBREGION_OPAL_S3_STATE, 0x9ff000, 0x1000 },
		{ SMM_SUBREGION_CACHE, 0xa00000, 0x200000 },
		{ SMM_SUBREGION_CHIPSET, 0xc00000, 0x400000 },
	};
	uintptr_t base;
	size_t size;

	region_base = 0x7b000000;
	region_size = 0x1000000;
	for (size_t i = 0; i < ARRAY_SIZE(expected); i++) {
		assert_int_equal(smm_subregion(expected[i].subregion, &base, &size), 0);
		assert_int_equal(base, region_base + expected[i].offset);
		assert_int_equal(size, expected[i].size);
		assert_true(base >= region_base && base + size <= region_base + region_size);
		if (i)
			assert_int_equal(expected[i - 1].offset + expected[i - 1].size,
					 expected[i].offset);
	}
}

static void reject_invalid_layout(void **state)
{
	uintptr_t base = 0;
	size_t size = 0;

	/* The old candidate reserved more than the complete 8 MiB TSEG. */
	region_base = 0x7b800000;
	region_size = 0x800000;
	expect_assert_failure(smm_subregion(SMM_SUBREGION_HANDLER, &base, &size));
	assert_int_equal(base, 0);
	assert_int_equal(size, 0);

	/* Equality still leaves no room for the SMM handler. */
	region_base = 0;
	region_size = 0xa01000;
	expect_assert_failure(smm_subregion(SMM_SUBREGION_HANDLER, &base, &size));

	region_base = 0x7b000001;
	region_size = 0x1000000;
	expect_assert_failure(smm_subregion(SMM_SUBREGION_PAYLOAD, &base, &size));
	region_size = 0;
	expect_assert_failure(smm_subregion(SMM_SUBREGION_PAYLOAD, &base, &size));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(valid_layout),
		cmocka_unit_test(reject_invalid_layout),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
