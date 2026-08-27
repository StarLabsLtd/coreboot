/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/coreboot_tables.h>
#include <commonlib/helpers.h>
#include <drivers/option/cfr_settings.h>
#include <tests/test.h>

static const struct sm_object setting = SM_DECLARE_BOOL({
	.flags = CFR_OPTFLAG_RUNTIME,
	.opt_name = "setting",
	.ui_name = "Setting",
	.default_value = true,
});

static const struct sm_object form = SM_DECLARE_FORM({
	.ui_name = "Settings",
	.obj_list = (const struct sm_object *[]) {
		&setting,
		NULL,
	},
});

static const struct sm_object *const forms[] = {
	&form,
};

static const struct cfr_settings_policy policy = {
	.token = 1,
	.option = &setting,
	.flags = CFR_SETTINGS_POLICY_READ | CFR_SETTINGS_POLICY_WRITE,
};

const struct sm_object *const *cfr_settings_policy_forms(size_t *num_forms)
{
	*num_forms = ARRAY_SIZE(forms);
	return forms;
}

const struct cfr_settings_policy *cfr_settings_policy_for_option(
	const struct sm_object *option)
{
	return option == policy.option ? &policy : NULL;
}

static void test_reject_serialization_past_limit(void **state)
{
	struct {
		struct lb_cfr root;
		uint8_t canary[64];
	} buffer;
	const uint8_t expected[sizeof(buffer.canary)] = { [0 ... sizeof(buffer.canary) - 1] = 0xa5 };

	memset(&buffer, 0xa5, sizeof(buffer));
	cfr_set_serialization_limit(sizeof(buffer.root));
	cfr_write_setup_menu(&buffer.root, NULL);

	assert_int_equal(buffer.root.tag, LB_TAG_CFR_ROOT);
	assert_int_equal(buffer.root.size, sizeof(buffer.root));
	assert_int_equal(buffer.root.checksum, 0);
	assert_memory_equal(buffer.canary, expected, sizeof(buffer.canary));
}

static void test_serialize_within_limit(void **state)
{
	struct {
		uint8_t data[512];
		uint8_t canary[64];
	} buffer = { 0 };
	const uint8_t expected_data[sizeof(buffer.data)] = {
		[0 ... sizeof(buffer.data) - 1] = 0xa5,
	};
	const uint8_t expected[sizeof(buffer.canary)] = { [0 ... sizeof(buffer.canary) - 1] = 0xa5 };
	struct lb_cfr *root = (struct lb_cfr *)buffer.data;
	size_t serialized_size;

	memset(buffer.canary, 0xa5, sizeof(buffer.canary));
	cfr_set_serialization_limit(sizeof(buffer.data));
	cfr_write_setup_menu(root, NULL);

	assert_int_equal(root->tag, LB_TAG_CFR_ROOT);
	assert_true(root->size > sizeof(*root));
	assert_true(root->size <= sizeof(buffer.data));
	serialized_size = root->size;

	memset(buffer.data, 0, serialized_size);
	memset(buffer.data + serialized_size, 0xa5,
	       sizeof(buffer.data) - serialized_size);
	cfr_set_serialization_limit(serialized_size);
	cfr_write_setup_menu(root, NULL);

	assert_int_equal(root->size, serialized_size);
	assert_memory_equal(buffer.data + serialized_size, expected_data,
			    sizeof(buffer.data) - serialized_size);
	assert_memory_equal(buffer.canary, expected, sizeof(buffer.canary));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_reject_serialization_past_limit),
		cmocka_unit_test(test_serialize_within_limit),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
