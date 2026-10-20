/* SPDX-License-Identifier: GPL-2.0-only */

#include <intelblocks/nvs.h>
#include <intelblocks/smm_gpio.h>
#include <soc/gpio_soc_defs.h>
#include <tests/test.h>

struct global_nvs *gnvs;

enum allowlist_kind {
	DENY_ALL,
	STARBOOK,
	STARFIGHTER,
};

static struct global_nvs test_gnvs;
static enum allowlist_kind allowlist;
static bool mutate_request;
static unsigned int hardware_accesses;
static gpio_t accessed_pad;
static int get_result;
static int get_value;
static int set_result;
static int set_value;

bool mainboard_smm_gpio_allowed(gpio_t pad)
{
	if (mutate_request) {
		gnvs->gpio_pad = GPP_B13;
		gnvs->gpio_op = SMM_GPIO_CLEAR;
	}

	if (allowlist == STARBOOK)
		return pad == GPP_B18 || pad == GPP_B19;
	if (allowlist == STARFIGHTER)
		return pad == GPP_B18 || pad == GPP_B19 || pad == GPP_B21 || pad == GPP_D22;
	return false;
}

int gpio_sbi_get_tx(gpio_t pad, int *value)
{
	hardware_accesses++;
	accessed_pad = pad;
	*value = get_value;
	return get_result;
}

int gpio_sbi_set_tx(gpio_t pad, int value)
{
	hardware_accesses++;
	accessed_pad = pad;
	set_value = value;
	return set_result;
}

static int setup(void **state)
{
	memset(&test_gnvs, 0, sizeof(test_gnvs));
	gnvs = &test_gnvs;
	allowlist = DENY_ALL;
	mutate_request = false;
	hardware_accesses = 0;
	accessed_pad = 0;
	get_result = 0;
	get_value = 0;
	set_result = 0;
	set_value = -1;
	return 0;
}

static void request(gpio_t pad, uint8_t op)
{
	gnvs->gpio_pad = pad;
	gnvs->gpio_op = op;
	gnvs->gpio_value = 0xa5;
	gnvs->gpio_status = 0;
	smm_gpio_handler();
}

static void default_allowlist_denies_without_access(void **state)
{
	request(GPP_B18, SMM_GPIO_GET);
	assert_int_equal(gnvs->gpio_status, 1);
	assert_int_equal(gnvs->gpio_value, 0xa5);
	assert_int_equal(hardware_accesses, 0);
}

static void board_allowlists_match_real_pads(void **state)
{
	const gpio_t starbook[] = { GPP_B18, GPP_B19 };
	const gpio_t starfighter[] = { GPP_B18, GPP_B19, GPP_B21, GPP_D22 };

	allowlist = STARBOOK;
	for (size_t i = 0; i < ARRAY_SIZE(starbook); i++) {
		request(starbook[i], SMM_GPIO_GET);
		assert_int_equal(gnvs->gpio_status, 0);
	}
	request(GPP_B21, SMM_GPIO_GET);
	assert_int_equal(gnvs->gpio_status, 1);

	allowlist = STARFIGHTER;
	for (size_t i = 0; i < ARRAY_SIZE(starfighter); i++) {
		request(starfighter[i], SMM_GPIO_GET);
		assert_int_equal(gnvs->gpio_status, 0);
	}
	request(GPP_B13, SMM_GPIO_GET);
	assert_int_equal(gnvs->gpio_status, 1);
}

static void rejects_invalid_pad_and_operation(void **state)
{
	allowlist = STARFIGHTER;
	request(UINT32_MAX, SMM_GPIO_GET);
	assert_int_equal(gnvs->gpio_status, 1);
	request(GPP_B18, 0xff);
	assert_int_equal(gnvs->gpio_status, 1);
	assert_int_equal(hardware_accesses, 0);
}

static void null_gnvs_is_ignored(void **state)
{
	gnvs = NULL;
	smm_gpio_handler();
	assert_int_equal(hardware_accesses, 0);
}

static void captured_request_cannot_be_redirected(void **state)
{
	allowlist = STARBOOK;
	mutate_request = true;
	request(GPP_B18, SMM_GPIO_SET);
	assert_int_equal(gnvs->gpio_status, 0);
	assert_int_equal(accessed_pad, GPP_B18);
	assert_int_equal(set_value, 1);
}

static void get_reports_zero_one_and_failure(void **state)
{
	allowlist = STARBOOK;
	get_value = 0;
	request(GPP_B18, SMM_GPIO_GET);
	assert_int_equal(gnvs->gpio_status, 0);
	assert_int_equal(gnvs->gpio_value, 0);

	get_value = 1;
	request(GPP_B18, SMM_GPIO_GET);
	assert_int_equal(gnvs->gpio_status, 0);
	assert_int_equal(gnvs->gpio_value, 1);

	get_result = -1;
	request(GPP_B18, SMM_GPIO_GET);
	assert_int_equal(gnvs->gpio_status, 1);
}

static void set_and_clear_propagate_failure(void **state)
{
	allowlist = STARBOOK;
	request(GPP_B18, SMM_GPIO_SET);
	assert_int_equal(gnvs->gpio_status, 0);
	assert_int_equal(set_value, 1);

	request(GPP_B18, SMM_GPIO_CLEAR);
	assert_int_equal(gnvs->gpio_status, 0);
	assert_int_equal(set_value, 0);

	set_result = -1;
	request(GPP_B18, SMM_GPIO_SET);
	assert_int_equal(gnvs->gpio_status, 1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(default_allowlist_denies_without_access, setup),
		cmocka_unit_test_setup(board_allowlists_match_real_pads, setup),
		cmocka_unit_test_setup(rejects_invalid_pad_and_operation, setup),
		cmocka_unit_test_setup(null_gnvs_is_ignored, setup),
		cmocka_unit_test_setup(captured_request_cannot_be_redirected, setup),
		cmocka_unit_test_setup(get_reports_zero_one_and_failure, setup),
		cmocka_unit_test_setup(set_and_clear_propagate_failure, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
