/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DRIVERS_OPTION_CFR_SETTINGS_H
#define DRIVERS_OPTION_CFR_SETTINGS_H

#include <drivers/option/cfr_frontend.h>
#include <types.h>

enum cfr_settings_policy_flags {
	CFR_SETTINGS_POLICY_READ = CFR_SETTINGS_ACCESS_READ,
	CFR_SETTINGS_POLICY_WRITE = CFR_SETTINGS_ACCESS_WRITE,
	CFR_SETTINGS_POLICY_RUNTIME_APPLY = 1 << 2,
};

struct cfr_settings_policy {
	uint32_t token;
	const struct sm_object *option;
	uint32_t flags;
};

struct lb_header;

/* A board without an override has an empty, fail-closed policy. */
const struct cfr_settings_policy *mainboard_cfr_settings_policy(size_t *num_entries);

/* Full SM_OBJ_FORM roots for the exact CFR tree, also compiled into SMM. */
const struct sm_object *const *mainboard_cfr_settings_forms(size_t *num_forms);

/* Optional board-specific validation in addition to generic CFR validation. */
enum cb_err mainboard_cfr_settings_validate(uint32_t token, uint32_t old_value,
					    uint32_t new_value);

/* Required for entries marked CFR_SETTINGS_POLICY_RUNTIME_APPLY. */
enum cb_err mainboard_cfr_settings_apply(uint32_t token, uint32_t value);
enum cb_err mainboard_cfr_settings_verify(uint32_t token, uint32_t value);

const struct cfr_settings_policy *cfr_settings_policy_for_token(uint32_t token);
const struct cfr_settings_policy *cfr_settings_policy_for_option(
	const struct sm_object *option);
bool cfr_settings_policy_can_write(const struct cfr_settings_policy *policy);
bool cfr_settings_value_is_valid(const struct sm_object *option, uint32_t value);

typedef enum cb_err (*cfr_settings_read_fn)(const struct sm_object *option,
					    uint32_t *value);
bool cfr_settings_dependencies_satisfied(const struct cfr_settings_policy *policy,
					 cfr_settings_read_fn read_option);

void cfr_settings_smm_execute(void);
void lb_cfr_settings(struct lb_header *header);

#if ENV_TEST
void cfr_settings_policy_reset_for_test(void);
void cfr_settings_smm_reset_for_test(void);
#endif

#endif /* DRIVERS_OPTION_CFR_SETTINGS_H */
