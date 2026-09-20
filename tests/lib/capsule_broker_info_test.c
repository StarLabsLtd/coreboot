/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_broker.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "capsule_broker_info_internal.h"
#include "payload_mm_fmp_owner_internal.h"

static bool owner_ready = true;
static bool protected_ok = true;
static bool staging_ok = true;
static bool overlap;
static bool read_ok = true;
static bool mutate_source;
static bool mutate_authority;
static bool mutate_current_version;
static bool mutate_capabilities;
static bool mutate_lifecycle;
static bool recurse;
static unsigned int reads;
static struct payload_mm_fmp_owner_record state;

void mock_assert(const int value, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!value)
		__builtin_trap();
}

static void put32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

bool payload_mm_fmp_owner_ready(void)
{
	return owner_ready;
}

bool payload_mm_authvar_buffers_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	(void)first;
	(void)first_size;
	(void)second;
	(void)second_size;
	return overlap;
}

bool payload_mm_fmp_state_staging_buffer(const void *buffer, size_t size)
{
	return staging_ok && buffer && size;
}

bool payload_mm_fmp_owner_record_valid(uint32_t key,
	const struct payload_mm_fmp_owner_record *record)
{
	return key == PAYLOAD_MM_FMP_STATE_KEY_STATE && record->sequence &&
		record->present <= 1 && !record->reserved && !record->reserved2 &&
		(!record->present ||
		 (record->attributes == PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
		  record->data_size == PAYLOAD_MM_FMP_STATE_WIRE_SIZE &&
		  record->data[0] <= 1 && record->data[1] <= 1 &&
		  record->data[2] <= 1 && record->data[3] <= 1));
}

enum cb_err payload_mm_fmp_owner_read(uint32_t key,
	struct payload_mm_fmp_owner_record *record)
{
	reads++;
	assert(key == PAYLOAD_MM_FMP_STATE_KEY_STATE);
	if (mutate_authority) {
		size_t size;
		uint8_t *authority = (uint8_t *)
			capsule_broker_info_test_authority(&size);

		assert(size > 0);
		authority[0] ^= 1;
	}
	if (mutate_current_version || mutate_capabilities) {
		size_t size;
		struct capsule_broker_info_policy *authority = (void *)
			capsule_broker_info_test_authority(&size);

		assert(size >= sizeof(*authority));
		if (mutate_current_version)
			authority->current_version++;
		if (mutate_capabilities)
			authority->capabilities = 0;
	}
	if (mutate_lifecycle) {
		size_t size;
		uint8_t *authority = (uint8_t *)
			capsule_broker_info_test_authority(&size);

		assert(size > sizeof(struct capsule_broker_info_policy));
		authority[sizeof(struct capsule_broker_info_policy)] = 0;
	}
	if (recurse) {
		struct capsule_broker_info_snapshot nested;

		assert(capsule_broker_info_read(&nested) == CB_ERR);
	}
	if (!read_ok)
		return CB_ERR;
	*record = state;
	return CB_SUCCESS;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	struct capsule_broker_info_policy *source = context;

	assert(storage && size);
	if (mutate_source)
		source->current_version++;
	if (mutate_authority)
		((uint8_t *)storage)[size - 1] ^= 1;
	return protected_ok;
}

static struct capsule_broker_info_policy policy(void)
{
	struct capsule_broker_info_policy value = {
		.revision = CAPSULE_BROKER_INFO_POLICY_REVISION,
		.size = sizeof(value),
		.hardware_instance = 0x1122334455667788ULL,
		.current_version = 11,
		.lowest_supported_version = 7,
		.image_size = 8 * 1024 * 1024,
		.capabilities = LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES,
	};

	value.image_type.b[0] = 1;
	return value;
}

static void expect_poisoned(void)
{
	struct {
		struct capsule_broker_info_policy policy;
		bool installed;
		bool install_attempted;
		bool busy;
	} authority;
	size_t size;
	const void *source = capsule_broker_info_test_authority(&size);
	const uint8_t zero[sizeof(authority.policy)] = { 0 };

	assert(size == sizeof(authority));
	memcpy(&authority, source, sizeof(authority));
	assert(!memcmp(&authority.policy, zero, sizeof(zero)));
	assert(!authority.installed && authority.install_attempted &&
		!authority.busy);
}

static void prepare_state(void)
{
	state = (struct payload_mm_fmp_owner_record) {
		.sequence = 9,
		.present = 1,
		.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		.data_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE,
	};
	state.data[0] = 1;
	state.data[1] = 1;
	state.data[2] = 1;
	state.data[3] = 1;
	put32(state.data + 4, 11);
	put32(state.data + 8, 8);
	put32(state.data + 12, CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS);
	put32(state.data + 16, 10);
}

int main(int argc, char **argv)
{
	struct capsule_broker_info_policy value = policy();
	struct capsule_broker_info_snapshot info;
	const char *test;

	assert(argc == 2);
	test = argv[1];
	prepare_state();
	if (!strcmp(test, "install-owner"))
		owner_ready = false;
	else if (!strcmp(test, "install-protection"))
		protected_ok = false;
	else if (!strcmp(test, "install-revision"))
		value.revision++;
	else if (!strcmp(test, "install-size"))
		value.size--;
	else if (!strcmp(test, "install-guid"))
		memset(&value.image_type, 0, sizeof(value.image_type));
	else if (!strcmp(test, "install-version"))
		value.current_version = 0;
	else if (!strcmp(test, "install-floor"))
		value.lowest_supported_version = value.current_version + 1;
	else if (!strcmp(test, "install-image-size"))
		value.image_size = 0;
	else if (!strcmp(test, "install-capabilities"))
		value.capabilities ^= LB_CAPSULE_BROKER_VERIFY_READBACK;
	else if (!strcmp(test, "install-reserved"))
		value.reserved[1] = 1;
	else if (!strcmp(test, "install-overlap"))
		overlap = true;
	else if (!strcmp(test, "install-source-mutation"))
		mutate_source = true;
	else if (!strcmp(test, "install-context-mutation"))
		mutate_source = true;
	else if (!strcmp(test, "install-authority-mutation"))
		mutate_authority = true;
	if (strncmp(test, "install-", 8) == 0) {
		assert(capsule_broker_info_policy_install(&value, protected_storage,
			&value) == CB_ERR);
		overlap = false;
		expect_poisoned();
		assert(capsule_broker_info_policy_install(&value, protected_storage,
			&value) == CB_ERR);
		memset(&info, 0xa5, sizeof(info));
		assert(capsule_broker_info_read(&info) == CB_ERR && !reads);
		for (size_t i = 0; i < sizeof(info); i++)
			assert(!((uint8_t *)&info)[i]);
		return 0;
	}
	assert(capsule_broker_info_policy_install(&value, protected_storage,
		&value) == CB_SUCCESS);
	assert(capsule_broker_info_policy_install(&value, protected_storage,
		&value) == CB_ERR);
	value.current_version = 99;
	if (!strcmp(test, "read-failure")) {
		read_ok = false;
	} else if (!strcmp(test, "read-record")) {
		state.reserved = 1;
	} else if (!strcmp(test, "read-absent")) {
		state.present = 0;
	} else if (!strcmp(test, "read-version-invalid")) {
		state.data[0] = 0;
	} else if (!strcmp(test, "read-version-stale")) {
		put32(state.data + 4, 10);
	} else if (!strcmp(test, "read-floor-newer")) {
		put32(state.data + 8, 12);
	} else if (!strcmp(test, "read-authority-mutation")) {
		mutate_authority = true;
	} else if (!strcmp(test, "read-current-mutation")) {
		mutate_current_version = true;
	} else if (!strcmp(test, "read-capabilities-mutation")) {
		mutate_capabilities = true;
	} else if (!strcmp(test, "read-lifecycle-mutation")) {
		mutate_lifecycle = true;
	} else if (!strcmp(test, "read-output")) {
		staging_ok = false;
	} else if (!strcmp(test, "read-reentry")) {
		recurse = true;
	} else if (!strcmp(test, "read-no-attempt")) {
		state.data[2] = 0;
		state.data[3] = 0;
	} else if (!strcmp(test, "read-updated-state")) {
		put32(state.data + 4, 12);
		put32(state.data + 8, 9);
		put32(state.data + 16, 12);
	} else if (!strcmp(test, "read-policy-floor")) {
		put32(state.data + 8, 6);
	}
	if (!strcmp(test, "read-null")) {
		assert(capsule_broker_info_read(NULL) == CB_ERR && !reads);
		return 0;
	}
	if (!strcmp(test, "read-misaligned")) {
		assert(capsule_broker_info_read((void *)((uintptr_t)&info + 1)) ==
			CB_ERR && !reads);
		return 0;
	}
	if (!strcmp(test, "read-output")) {
		memset(&info, 0xa5, sizeof(info));
		assert(capsule_broker_info_read(&info) == CB_ERR && !reads);
		for (size_t i = 0; i < sizeof(info); i++)
			assert(((uint8_t *)&info)[i] == 0xa5);
		return 0;
	}
	if (!strcmp(test, "read-failure") || !strcmp(test, "read-record") ||
	    !strcmp(test, "read-absent") ||
	    !strcmp(test, "read-version-invalid") ||
	    !strcmp(test, "read-version-stale") ||
	    !strcmp(test, "read-floor-newer") ||
	    !strcmp(test, "read-authority-mutation") ||
	    !strcmp(test, "read-current-mutation") ||
	    !strcmp(test, "read-capabilities-mutation") ||
	    !strcmp(test, "read-lifecycle-mutation")) {
		memset(&info, 0xa5, sizeof(info));
		assert(capsule_broker_info_read(&info) == CB_ERR);
		for (size_t i = 0; i < sizeof(info); i++)
			assert(!((uint8_t *)&info)[i]);
		if (strstr(test, "mutation")) {
			expect_poisoned();
			assert(reads == 1);
			mutate_authority = false;
			mutate_current_version = false;
			mutate_capabilities = false;
			mutate_lifecycle = false;
			memset(&info, 0xa5, sizeof(info));
			assert(capsule_broker_info_read(&info) == CB_ERR && reads == 1);
			for (size_t i = 0; i < sizeof(info); i++)
				assert(!((uint8_t *)&info)[i]);
			assert(capsule_broker_info_policy_install(&value,
				protected_storage, &value) == CB_ERR);
			expect_poisoned();
		}
		return 0;
	}
	assert(capsule_broker_info_read(&info) == CB_SUCCESS && reads == 1);
	assert(info.image_type.b[0] == 1);
	assert(info.hardware_instance == 0x1122334455667788ULL);
	assert(info.current_version == 11);
	assert(info.lowest_supported_version ==
		(!strcmp(test, "read-updated-state") ? 9 :
		 !strcmp(test, "read-policy-floor") ? 7 : 8));
	assert(info.image_size == 8 * 1024 * 1024);
	assert(info.capabilities == LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES);
	if (!strcmp(test, "read-no-attempt")) {
		assert(!info.state_flags && !info.last_attempt_version &&
			!info.last_attempt_status);
	} else {
		assert(info.state_flags == CAPSULE_BROKER_INFO_STATE_VALID_FLAGS);
		assert(info.last_attempt_version ==
			(!strcmp(test, "read-updated-state") ? 12 : 10));
		assert(info.last_attempt_status ==
			CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS);
	}
	return 0;
}
