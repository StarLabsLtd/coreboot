/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/cfr.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <drivers/efi/option.h>
#include <drivers/option/cfr_settings.h>
#include <string.h>

static bool transaction_faulted;
static bool response_cached;
static struct cfr_settings_mailbox cached_response;

#if ENV_TEST
void cfr_settings_smm_reset_for_test(void)
{
	transaction_faulted = false;
	response_cached = false;
	memset(&cached_response, 0, sizeof(cached_response));
}
#endif

static uint32_t option_flags(const struct sm_object *option)
{
	switch (option->kind) {
	case SM_OBJ_ENUM:
		return option->sm_enum.flags;
	case SM_OBJ_NUMBER:
		return option->sm_number.flags;
	case SM_OBJ_BOOL:
		return option->sm_bool.flags;
	default:
		return 0;
	}
}

static const char *option_name(const struct sm_object *option)
{
	switch (option->kind) {
	case SM_OBJ_ENUM:
		return option->sm_enum.opt_name;
	case SM_OBJ_NUMBER:
		return option->sm_number.opt_name;
	case SM_OBJ_BOOL:
		return option->sm_bool.opt_name;
	default:
		return NULL;
	}
}

static uint32_t option_default(const struct sm_object *option)
{
	switch (option->kind) {
	case SM_OBJ_ENUM:
		return option->sm_enum.default_value;
	case SM_OBJ_NUMBER:
		return option->sm_number.default_value;
	case SM_OBJ_BOOL:
		return option->sm_bool.default_value;
	default:
		return 0;
	}
}

static enum cb_err read_option(const struct sm_object *option, uint32_t *value)
{
	enum cb_err ret = efi_option_get_uint(option_name(option), value);

	if (ret == CB_EFI_FVH_INVALID || ret == CB_EFI_CHECKSUM_INVALID ||
	    ret == CB_EFI_VS_CORRUPTED_INVALID ||
	    ret == CB_EFI_VS_NOT_FORMATTED_INVALID) {
		ret = efi_option_initialize_store();
		if (ret == CB_SUCCESS)
			ret = efi_option_get_uint(option_name(option), value);
	}

	if (ret == CB_EFI_OPTION_NOT_FOUND) {
		*value = option_default(option);
		return CB_SUCCESS;
	}

	return ret;
}

static bool persist_and_verify(const struct sm_object *option, uint32_t value)
{
	uint32_t observed;

	if (efi_option_set_uint(option_name(option), value) != CB_SUCCESS)
		return false;

	return read_option(option, &observed) == CB_SUCCESS && observed == value;
}

static bool apply_and_verify(const struct cfr_settings_policy *policy, uint32_t value)
{
	enum cb_err apply_ret = mainboard_cfr_settings_apply(policy->token, value);
	enum cb_err verify_ret = mainboard_cfr_settings_verify(policy->token, value);

	return apply_ret == CB_SUCCESS && verify_ret == CB_SUCCESS;
}

static uint32_t rollback(const struct cfr_settings_policy *policy, uint32_t old_value)
{
	bool persistence_restored = persist_and_verify(policy->option, old_value);
	bool runtime_restored = true;

	if (policy->flags & CFR_SETTINGS_POLICY_RUNTIME_APPLY)
		runtime_restored = apply_and_verify(policy, old_value);

	if (persistence_restored && runtime_restored)
		return CFR_SETTINGS_STATUS_ROLLED_BACK;

	transaction_faulted = true;
	return CFR_SETTINGS_STATUS_INDETERMINATE;
}

static bool mailbox_reserved_is_zero(const struct cfr_settings_mailbox *mailbox)
{
	for (size_t i = 0; i < ARRAY_SIZE(mailbox->reserved); i++)
		if (mailbox->reserved[i])
			return false;

	return true;
}

static bool request_header_is_valid(const struct cfr_settings_mailbox *request)
{
	return request->version == CFR_SETTINGS_VERSION &&
		request->size == sizeof(*request) && request->sequence &&
		mailbox_reserved_is_zero(request);
}

static bool request_command_is_valid(const struct cfr_settings_mailbox *request)
{
	return request->command == CFR_SETTINGS_CMD_GET ||
		request->command == CFR_SETTINGS_CMD_SET;
}

static bool sequence_is_newer(uint64_t sequence, uint64_t previous)
{
	const uint64_t distance = sequence - previous;

	return distance && distance < (1ULL << 63);
}

static void set_current(struct cfr_settings_mailbox *response, uint32_t value)
{
	response->current_value = value;
	response->response_flags |= CFR_SETTINGS_RESP_CURRENT_VALID;
}

static void refresh_current(struct cfr_settings_mailbox *response,
			    const struct sm_object *option)
{
	uint32_t value;

	response->response_flags &= ~CFR_SETTINGS_RESP_CURRENT_VALID;
	if (read_option(option, &value) == CB_SUCCESS)
		set_current(response, value);
}

static void execute_request(struct cfr_settings_mailbox *response)
{
	const struct cfr_settings_policy *policy;
	const struct sm_object *option;
	uint32_t current;

	if (!request_header_is_valid(response)) {
		response->status = CFR_SETTINGS_STATUS_INVALID_MAILBOX;
		return;
	}

	if (!request_command_is_valid(response)) {
		response->status = CFR_SETTINGS_STATUS_INVALID_COMMAND;
		return;
	}

	policy = cfr_settings_policy_for_token(response->token);
	if (!policy) {
		response->status = CFR_SETTINGS_STATUS_INVALID_TOKEN;
		return;
	}
	option = policy->option;

	if (read_option(option, &current) != CB_SUCCESS) {
		response->status = CFR_SETTINGS_STATUS_STORAGE_ERROR;
		return;
	}
	set_current(response, current);

	if (response->command == CFR_SETTINGS_CMD_GET) {
		response->status = CFR_SETTINGS_STATUS_VALUE;
		return;
	}

	if (transaction_faulted) {
		response->status = CFR_SETTINGS_STATUS_INDETERMINATE;
		return;
	}

	if (!cfr_settings_policy_can_write(policy)) {
		response->status = CFR_SETTINGS_STATUS_DENIED;
		return;
	}

	if (option_flags(option) & (CFR_OPTFLAG_READONLY | CFR_OPTFLAG_INACTIVE |
				    CFR_OPTFLAG_SUPPRESS | CFR_OPTFLAG_VOLATILE)) {
		response->status = CFR_SETTINGS_STATUS_DENIED;
		return;
	}

	if (current != response->expected_value) {
		response->status = CFR_SETTINGS_STATUS_CONFLICT;
		return;
	}

	if (!cfr_settings_value_is_valid(option, response->value)) {
		response->status = CFR_SETTINGS_STATUS_INVALID_VALUE;
		return;
	}

	if (!cfr_settings_dependencies_satisfied(policy, read_option)) {
		response->status = CFR_SETTINGS_STATUS_DEPENDENCY_FAILED;
		return;
	}

	if (mainboard_cfr_settings_validate(policy->token, current,
					    response->value) != CB_SUCCESS) {
		response->status = CFR_SETTINGS_STATUS_INVALID_VALUE;
		return;
	}

	if (current == response->value) {
		response->status = CFR_SETTINGS_STATUS_UNCHANGED;
		return;
	}

	response->response_flags &= ~CFR_SETTINGS_RESP_CURRENT_VALID;
	if (!persist_and_verify(option, response->value)) {
		response->status = rollback(policy, current);
		refresh_current(response, option);
		return;
	}
	set_current(response, response->value);

	if (!(policy->flags & CFR_SETTINGS_POLICY_RUNTIME_APPLY)) {
		response->status = CFR_SETTINGS_STATUS_REBOOT_REQUIRED;
		return;
	}

	if (apply_and_verify(policy, response->value)) {
		response->status = CFR_SETTINGS_STATUS_APPLIED;
		return;
	}

	response->status = rollback(policy, current);
	refresh_current(response, option);
}

static bool request_matches_cached_response(const struct cfr_settings_mailbox *request)
{
	return request->version == cached_response.version &&
		request->size == cached_response.size &&
		request->sequence == cached_response.sequence &&
		request->command == cached_response.command &&
		request->token == cached_response.token &&
		request->expected_value == cached_response.expected_value &&
		request->value == cached_response.value &&
		memcmp(request->reserved, cached_response.reserved,
		       sizeof(request->reserved)) == 0;
}

void cfr_settings_smm_execute(void)
{
	struct cfr_settings_mailbox request;
	struct cfr_settings_mailbox *mailbox;
	uintptr_t base;
	size_t size;

	smm_get_cfr_settings_mailbox(&base, &size);
	if (!base || size != sizeof(request) ||
	    smm_points_to_smram((const void *)base, sizeof(request))) {
		printk(BIOS_ERR, "CFR settings: invalid fixed mailbox\n");
		return;
	}

	mailbox = (struct cfr_settings_mailbox *)base;
	memcpy(&request, mailbox, sizeof(request));
	mailbox->status = CFR_SETTINGS_STATUS_BUSY;

	request.status = CFR_SETTINGS_STATUS_BUSY;
	request.current_value = 0;
	request.response_flags = transaction_faulted ? CFR_SETTINGS_RESP_FAULTED : 0;
	if (response_cached && request.sequence == cached_response.sequence) {
		if (request_matches_cached_response(&request))
			request = cached_response;
		else
			request.status = CFR_SETTINGS_STATUS_INVALID_MAILBOX;
	} else if (!request_header_is_valid(&request) ||
		   !request_command_is_valid(&request)) {
		execute_request(&request);
	} else if (response_cached &&
		   !sequence_is_newer(request.sequence, cached_response.sequence)) {
		request.status = CFR_SETTINGS_STATUS_INVALID_MAILBOX;
	} else {
		execute_request(&request);
		if (transaction_faulted)
			request.response_flags |= CFR_SETTINGS_RESP_FAULTED;
		cached_response = request;
		response_cached = true;
	}
	if (transaction_faulted)
		request.response_flags |= CFR_SETTINGS_RESP_FAULTED;

	memcpy(mailbox, &request, sizeof(request));
}
