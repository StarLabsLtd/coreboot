/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <drivers/option/cfr_settings.h>
#include <tests/test.h>

static const struct sm_object controller = SM_DECLARE_BOOL({
	.flags = CFR_OPTFLAG_RUNTIME,
	.opt_name = "controller",
	.ui_name = "Controller",
	.default_value = true,
});

static const struct sm_object intermediate = SM_DECLARE_BOOL({
	.flags = CFR_OPTFLAG_RUNTIME,
	.opt_name = "intermediate",
	.ui_name = "Intermediate",
	.default_value = true,
}, WITH_DEP(&controller));

static const struct sm_object target = SM_DECLARE_BOOL({
	.flags = CFR_OPTFLAG_RUNTIME,
	.opt_name = "target",
	.ui_name = "Target",
	.default_value = false,
}, WITH_DEP(&intermediate));

static const struct sm_object full_range = SM_DECLARE_NUMBER({
	.flags = CFR_OPTFLAG_RUNTIME,
	.opt_name = "full_range",
	.ui_name = "Full range",
	.default_value = UINT32_MAX,
	.min = 0,
	.max = 0,
	.step = 1,
});

static const struct sm_object valid_form = SM_DECLARE_FORM({
	.ui_name = "Valid policy",
	.obj_list = (const struct sm_object *[]) {
		&controller,
		&intermediate,
		&target,
		&full_range,
		NULL,
	},
});

static const struct sm_object *const valid_roots[] = {
	&valid_form,
	NULL,
};

static const struct cfr_settings_policy valid_policy[] = {
	{ .token = 1, .option = &controller, .flags = CFR_SETTINGS_POLICY_READ },
	{ .token = 2, .option = &intermediate, .flags = CFR_SETTINGS_POLICY_READ },
	{
		.token = 3,
		.option = &target,
		.flags = CFR_SETTINGS_POLICY_READ | CFR_SETTINGS_POLICY_WRITE,
	},
	{
		.token = 4,
		.option = &full_range,
		.flags = CFR_SETTINGS_POLICY_READ | CFR_SETTINGS_POLICY_WRITE,
	},
};

static const struct cfr_settings_policy duplicate_token_policy[] = {
	{ .token = 1, .option = &controller, .flags = CFR_SETTINGS_POLICY_READ },
	{ .token = 1, .option = &intermediate, .flags = CFR_SETTINGS_POLICY_READ },
};

static const struct cfr_settings_policy missing_controller_policy[] = {
	{
		.token = 3,
		.option = &target,
		.flags = CFR_SETTINGS_POLICY_READ | CFR_SETTINGS_POLICY_WRITE,
	},
};

static const struct sm_object cycle_b;
static const struct sm_object cycle_a = SM_DECLARE_BOOL({
	.flags = CFR_OPTFLAG_RUNTIME,
	.opt_name = "cycle_a",
	.ui_name = "Cycle A",
	.default_value = true,
}, WITH_DEP(&cycle_b));

static const struct sm_object cycle_b = SM_DECLARE_BOOL({
	.flags = CFR_OPTFLAG_RUNTIME,
	.opt_name = "cycle_b",
	.ui_name = "Cycle B",
	.default_value = true,
}, WITH_DEP(&cycle_a));

static const struct sm_object cycle_form = SM_DECLARE_FORM({
	.ui_name = "Cycle",
	.obj_list = (const struct sm_object *[]) {
		&cycle_a,
		&cycle_b,
		NULL,
	},
});

static const struct sm_object *const cycle_roots[] = {
	&cycle_form,
	NULL,
};

static const struct cfr_settings_policy cycle_policy[] = {
	{ .token = 1, .option = &cycle_a, .flags = CFR_SETTINGS_POLICY_READ },
	{ .token = 2, .option = &cycle_b, .flags = CFR_SETTINGS_POLICY_READ },
};

static const struct sm_object invalid_default = SM_DECLARE_NUMBER({
	.flags = CFR_OPTFLAG_RUNTIME,
	.opt_name = "invalid_default",
	.ui_name = "Invalid default",
	.default_value = 3,
	.min = 1,
	.max = 2,
	.step = 1,
});

static const struct sm_object invalid_default_form = SM_DECLARE_FORM({
	.ui_name = "Invalid default",
	.obj_list = (const struct sm_object *[]) {
		&invalid_default,
		NULL,
	},
});

static const struct sm_object *const invalid_default_roots[] = {
	&invalid_default_form,
	NULL,
};

static const struct cfr_settings_policy invalid_default_policy[] = {
	{ .token = 1, .option = &invalid_default, .flags = CFR_SETTINGS_POLICY_READ },
};

static const struct cfr_settings_policy *active_policy;
static size_t active_policy_entries;
static const struct sm_object *const *active_roots;
static size_t active_root_entries;
static bool controller_value;
static bool intermediate_value;

const struct cfr_settings_policy *mainboard_cfr_settings_policy(size_t *num_entries)
{
	*num_entries = active_policy_entries;
	return active_policy;
}

const struct sm_object *const *mainboard_cfr_settings_forms(size_t *num_forms)
{
	*num_forms = active_root_entries;
	return active_roots;
}

static enum cb_err read_dependency(const struct sm_object *option, uint32_t *value)
{
	if (option == &controller)
		*value = controller_value;
	else if (option == &intermediate)
		*value = intermediate_value;
	else
		return CB_ERR_ARG;

	return CB_SUCCESS;
}

static void use_policy_with_roots(const struct cfr_settings_policy *policy,
				  size_t entries,
				  const struct sm_object *const *policy_roots,
				  size_t root_entries)
{
	active_policy = policy;
	active_policy_entries = entries;
	active_roots = policy_roots;
	active_root_entries = root_entries;
	cfr_settings_policy_reset_for_test();
}

static void use_policy(const struct cfr_settings_policy *policy, size_t entries)
{
	use_policy_with_roots(policy, entries, valid_roots,
			      ARRAY_SIZE(valid_roots) - 1);
}

static void test_valid_policy_and_full_range(void **state)
{
	use_policy(valid_policy, ARRAY_SIZE(valid_policy));

	assert_ptr_equal(cfr_settings_policy_for_token(3), &valid_policy[2]);
	assert_true(cfr_settings_value_is_valid(&full_range, UINT32_MAX));
	assert_true(cfr_settings_value_is_valid(&full_range, 0));
}

static void test_reject_duplicate_token(void **state)
{
	use_policy(duplicate_token_policy, ARRAY_SIZE(duplicate_token_policy));
	assert_true(cfr_settings_policy_for_token(1) == NULL);
}

static void test_reject_missing_controller(void **state)
{
	use_policy(missing_controller_policy, ARRAY_SIZE(missing_controller_policy));
	assert_true(cfr_settings_policy_for_token(3) == NULL);
}

static void test_enforce_transitive_dependency(void **state)
{
	use_policy(valid_policy, ARRAY_SIZE(valid_policy));
	intermediate_value = true;
	controller_value = false;
	assert_false(cfr_settings_dependencies_satisfied(&valid_policy[2],
							 read_dependency));

	controller_value = true;
	assert_true(cfr_settings_dependencies_satisfied(&valid_policy[2],
							read_dependency));
}

static void test_reject_dependency_cycle(void **state)
{
	use_policy_with_roots(cycle_policy, ARRAY_SIZE(cycle_policy), cycle_roots,
			      ARRAY_SIZE(cycle_roots) - 1);
	assert_true(cfr_settings_policy_for_token(1) == NULL);
}

static void test_reject_invalid_default(void **state)
{
	use_policy_with_roots(invalid_default_policy, ARRAY_SIZE(invalid_default_policy),
			      invalid_default_roots,
			      ARRAY_SIZE(invalid_default_roots) - 1);
	assert_true(cfr_settings_policy_for_token(1) == NULL);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_valid_policy_and_full_range),
		cmocka_unit_test(test_reject_duplicate_token),
		cmocka_unit_test(test_reject_missing_controller),
		cmocka_unit_test(test_enforce_transitive_dependency),
		cmocka_unit_test(test_reject_dependency_cycle),
		cmocka_unit_test(test_reject_invalid_default),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
