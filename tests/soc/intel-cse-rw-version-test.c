/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <tests/test.h>

#include "../../src/soc/intel/common/block/cse/cse.c"

static struct cse_specific_info test_info;
static bool cbmem_record_present;
static char log_buffer[128];

void *cbmem_find(u32 id)
{
	assert_int_equal(id, CBMEM_ID_CSE_INFO);
	return cbmem_record_present ? &test_info : NULL;
}

int printk(int msg_level, const char *fmt, ...)
{
	va_list args;
	int ret;

	assert_int_equal(msg_level, BIOS_DEBUG);
	va_start(args, fmt);
	ret = vsnprintf(log_buffer, sizeof(log_buffer), fmt, args);
	va_end(args);
	return ret;
}

static int setup(void **state)
{
	memset(&test_info, 0, sizeof(test_info));
	memset(log_buffer, 0, sizeof(log_buffer));
	test_info.cse_fwp_version.cur_cse_fw_version.major = 12;
	test_info.cse_fwp_version.cur_cse_fw_version.minor = 34;
	test_info.cse_fwp_version.cur_cse_fw_version.hotfix = 56;
	test_info.cse_fwp_version.cur_cse_fw_version.build = 7890;
	cbmem_record_present = true;
	return 0;
}

static void record_present(void **state)
{
	intel_cse_get_rw_version();

	if (CONFIG(SOC_INTEL_CSE_LITE_SYNC_BY_PAYLOAD))
		assert_string_equal(log_buffer, "");
	else
		assert_string_equal(log_buffer, "CSE RW Firmware Version: 12.34.56.7890\n");
}

static void record_absent(void **state)
{
	cbmem_record_present = false;

	intel_cse_get_rw_version();

	assert_string_equal(log_buffer, "");
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(record_present, setup),
		cmocka_unit_test_setup(record_absent, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
