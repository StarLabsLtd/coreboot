/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <tests/test.h>

static bool resume, ram_ready;
static unsigned int ram_checks, smm_calls, posts;
static enum cb_err smm_result;
static bool mock_is_resume(void) { return resume; }
#define acpi_is_wakeup_s3 mock_is_resume
#define main coreboot_main
#include <soc/amd/common/block/cpu/smm/finalize.c>
#undef main

void acpi_clear_pm_gpe_status(void) { }
void acpi_disable_sci(void) { }
void acpi_enable_sci(void) { }
void fch_enable_power_button_smi(void) { }
bool efi_variable_service_ram_ready(void)
{
	ram_checks++;
	return ram_ready;
}
enum cb_err apm_control(u8 cmd)
{
	assert_int_equal(cmd, APM_CNT_FINALIZE);
	assert_int_equal(ram_checks, resume ? 0 : 1);
	smm_calls++;
	return smm_result;
}
void post_code(u8 value)
{
	assert_int_equal(value, POSTCODE_OS_BOOT);
	posts++;
}
void die(const char *message, ...)
{
	mock_assert(false, "die", __FILE__, __LINE__);
	__builtin_unreachable();
}
static int setup(void **unused)
{
	resume = false;
	ram_ready = true;
	ram_checks = smm_calls = posts = 0;
	smm_result = CB_SUCCESS;
	return 0;
}
static void normal_handoff(void **unused)
{
	soc_finalize(NULL);
	assert_int_equal(ram_checks, CONFIG(ASS) ? 1 : 0);
	assert_int_equal(smm_calls, CONFIG(ASS) ? 1 : 0);
	assert_int_equal(posts, 1);
}
static void reserved_ram_stops_initialization(void **unused)
{
	if (!CONFIG(ASS))
		return;
	ram_ready = false;
	expect_assert_failure(soc_finalize(NULL));
	assert_int_equal(smm_calls, 0);
	assert_int_equal(posts, 0);
}
static void failed_mm_stops_payload(void **unused)
{
	if (!CONFIG(ASS))
		return;
	smm_result = CB_ERR;
	expect_assert_failure(soc_finalize(NULL));
	assert_int_equal(smm_calls, 1);
	assert_int_equal(posts, 0);
}
static void resume_retains_resource_map(void **unused)
{
	resume = true;
	soc_finalize(NULL);
	assert_int_equal(ram_checks, 0);
	assert_int_equal(smm_calls, CONFIG(ASS) ? 1 : 0);
	assert_int_equal(posts, 1);
}
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(normal_handoff, setup),
		cmocka_unit_test_setup(reserved_ram_stops_initialization, setup),
		cmocka_unit_test_setup(failed_mm_stops_payload, setup),
		cmocka_unit_test_setup(resume_retains_resource_map, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
