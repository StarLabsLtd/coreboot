/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static const uint8_t control_guid[16] = {
	0xbe, 0x39, 0x09, 0xe2, 0xd4, 0x32, 0xbe, 0x41,
	0xa1, 0x50, 0x89, 0x7f, 0x85, 0xd4, 0x98, 0x29,
};
static const uint8_t lock_guid[16] = {
	0xcf, 0x3c, 0x98, 0xbb, 0x1d, 0x15, 0xe1, 0x40,
	0xa0, 0x7b, 0x4a, 0x17, 0xbe, 0x16, 0x82, 0x92,
};
static const uint16_t control_name[] = {
	'M', 'e', 'm', 'o', 'r', 'y', 'O', 'v', 'e', 'r', 'w', 'r', 'i', 't',
	'e', 'R', 'e', 'q', 'u', 'e', 's', 't', 'C', 'o', 'n', 't', 'r', 'o',
	'l', 0U,
};
static const uint16_t lock_name[] = {
	'M', 'e', 'm', 'o', 'r', 'y', 'O', 'v', 'e', 'r', 'w', 'r', 'i', 't',
	'e', 'R', 'e', 'q', 'u', 'e', 's', 't', 'C', 'o', 'n', 't', 'r', 'o',
	'l', 'L', 'o', 'c', 'k', 0U,
};
static const uint32_t attributes =
	PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
	PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
	PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;

static struct payload_mm_authvar_mor_request request_for(
	enum payload_mm_authvar_mor_variable variable, const void *data,
	size_t data_size)
{
	struct payload_mm_authvar_mor_request request = {
		.name = variable == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL ?
			control_name : lock_name,
		.name_size = variable == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL ?
			sizeof(control_name) : sizeof(lock_name),
		.attributes = attributes,
		.data = data,
		.data_size = data_size,
	};

	memcpy(request.vendor_guid,
		variable == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL ?
		control_guid : lock_guid, sizeof(request.vendor_guid));
	return request;
}

static struct payload_mm_authvar_mor_state supported(void)
{
	return (struct payload_mm_authvar_mor_state) {
		.generation = 1U,
		.initialized = true,
		.supported = true,
	};
}

static enum payload_mm_authvar_mor_finalize_result finalize_init(
	struct payload_mm_authvar_mor_state *state,
	const struct payload_mm_authvar_mor_boot_snapshot *snapshot,
	const struct payload_mm_authvar_mor_plan *plan, bool durable)
{
	struct payload_mm_authvar_mor_finalize_input input = {
		.kind = PAYLOAD_MM_AUTHVAR_MOR_INPUT_INIT,
		.init = *snapshot,
	};
	return payload_mm_authvar_mor_finalize(state, &input, plan, durable);
}

static enum payload_mm_authvar_mor_finalize_result finalize_set(
	struct payload_mm_authvar_mor_state *state,
	const struct payload_mm_authvar_mor_request *request,
	const struct payload_mm_authvar_mor_plan *plan, bool durable)
{
	struct payload_mm_authvar_mor_finalize_input input = {
		.kind = PAYLOAD_MM_AUTHVAR_MOR_INPUT_SET,
		.set = request,
	};
	return payload_mm_authvar_mor_finalize(state, &input, plan, durable);
}

static enum payload_mm_authvar_mor_finalize_result finalize_ready(
	struct payload_mm_authvar_mor_state *state,
	bool present, uint32_t current_attributes, size_t size, uint8_t value,
	const struct payload_mm_authvar_mor_plan *plan, bool durable)
{
	struct payload_mm_authvar_mor_finalize_input input = {
		.kind = PAYLOAD_MM_AUTHVAR_MOR_INPUT_READY_TO_BOOT,
		.ready = {
			.control_present = present,
			.control_attributes = current_attributes,
			.control_size = size,
			.control_value = value,
		},
	};
	return payload_mm_authvar_mor_finalize(state, &input, plan, durable);
}

static void classifier(void)
{
	uint16_t near_name[ARRAY_SIZE(control_name)];
	uint8_t near_guid[16];

	assert(payload_mm_authvar_mor_classify(control_guid, control_name,
		sizeof(control_name)) == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL);
	assert(payload_mm_authvar_mor_classify(lock_guid, lock_name,
		sizeof(lock_name)) == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK);
	memcpy(near_guid, control_guid, sizeof(near_guid));
	near_guid[0] ^= 1U;
	assert(payload_mm_authvar_mor_classify(near_guid, control_name,
		sizeof(control_name)) == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE);
	memcpy(near_name, control_name, sizeof(near_name));
	near_name[1] ^= 1U;
	assert(payload_mm_authvar_mor_classify(control_guid, near_name,
		sizeof(near_name)) == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE);
	assert(payload_mm_authvar_mor_classify(NULL, control_name,
		sizeof(control_name)) == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE);
	assert(payload_mm_authvar_mor_classify(control_guid,
		(const void *)(uintptr_t)(UINTPTR_MAX - 1U), 4U) ==
		PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE);
	assert(payload_mm_authvar_mor_classify(
		(const void *)(uintptr_t)(UINTPTR_MAX - 7U), control_name,
		sizeof(control_name)) == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE);
}

static void initialization(void)
{
	struct payload_mm_authvar_mor_state state = { 0 };
	struct payload_mm_authvar_mor_boot_snapshot snapshot = {
		.phase = PAYLOAD_MM_AUTHVAR_MOR_INIT_END_OF_DXE,
		.trusted_platform_support = true,
		.entry_control_present = true,
		.control_present = true,
		.lock_present = true,
		.control_attributes = attributes,
		.lock_attributes = attributes,
		.control_size = 1U,
		.lock_size = 1U,
		.control_value = 0xffU,
		.entry_control_value = 0xffU,
	};
	struct payload_mm_authvar_mor_plan plan;

	assert(payload_mm_authvar_mor_init_plan(&state, &snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(plan.mutation_count == 1U && plan.requires_durable_commit &&
		plan.mutations[0].kind == PAYLOAD_MM_AUTHVAR_MOR_MUTATION_WRITE &&
		plan.mutations[0].variable == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK &&
		plan.mutations[0].value == 0U && plan.projected_state.supported &&
		plan.projected_state.entry_clear_pending);
	assert(!finalize_init(&state, &snapshot, &plan, false));
	assert(!state.initialized);
	assert(finalize_init(&state, &snapshot, &plan, true));
	assert(state.initialized && state.supported && state.entry_clear_pending);
	state = (struct payload_mm_authvar_mor_state) { 0 };
	snapshot.phase = (enum payload_mm_authvar_mor_init_phase)-1;
	assert(payload_mm_authvar_mor_init_plan(&state, &snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	snapshot.phase = PAYLOAD_MM_AUTHVAR_MOR_INIT_END_OF_DXE;

	state = (struct payload_mm_authvar_mor_state) { 0 };
	snapshot.phase = PAYLOAD_MM_AUTHVAR_MOR_INIT_READY_TO_BOOT_FALLBACK;
	snapshot.trusted_platform_support = false;
	assert(payload_mm_authvar_mor_init_plan(&state, &snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(plan.mutation_count == 2U && !plan.projected_state.supported &&
		plan.projected_state.lock_state ==
			PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED &&
		plan.mutations[0].kind == PAYLOAD_MM_AUTHVAR_MOR_MUTATION_DELETE &&
		plan.mutations[1].kind == PAYLOAD_MM_AUTHVAR_MOR_MUTATION_DELETE);
	assert(finalize_init(&state, &snapshot, &plan, true));
	assert(payload_mm_authvar_mor_init_plan(&state, &snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && !plan.mutation_count);
}

static void control(void)
{
	static const uint8_t values[] = { 0U, 1U, 2U, 0xffU };
	struct payload_mm_authvar_mor_state state = supported();
	struct payload_mm_authvar_mor_plan plan;
	struct payload_mm_authvar_mor_request request;

	for (size_t i = 0U; i < ARRAY_SIZE(values); i++) {
		request = request_for(PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL,
			&values[i], 1U);
		assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(plan.matched && plan.pass_to_store &&
			plan.requires_durable_commit &&
			plan.projected_state.control_dirty);
		assert(!finalize_set(&state, &request, &plan, false));
	}
	request.attributes ^= PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	request.attributes = 0U;
	request.data_size = 0U;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	state.lock_state = PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITHOUT_KEY;
	request = request_for(PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL, values, 1U);
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED);
	state.supported = false;
	state.lock_state = PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);
}

static void lock(void)
{
	static const uint8_t key[PAYLOAD_MM_AUTHVAR_MOR_KEY_SIZE] = {
		1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
	};
	uint8_t wrong[sizeof(key)];
	uint8_t value;
	struct payload_mm_authvar_mor_state state = supported();
	struct payload_mm_authvar_mor_state keyed;
	struct payload_mm_authvar_mor_plan plan;
	struct payload_mm_authvar_mor_request request;

	value = 2U;
	request = request_for(PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK, &value, 1U);
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	value = 1U;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(plan.mutations[0].value == 1U &&
		plan.projected_state.lock_state ==
			PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITHOUT_KEY);
	assert(finalize_set(&state, &request, &plan, true));
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED);

	state = supported();
	request = request_for(PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK, key,
		sizeof(key));
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(plan.mutations[0].value == 2U &&
		plan.projected_state.lock_state == PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITH_KEY);
	assert(finalize_set(&state, &request, &plan, true));
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && plan.mutations[0].value == 0U &&
		plan.projected_state.lock_state == PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED);

	state = plan.projected_state;
	request = request_for(PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK,
		(uint8_t[8]) { 0 }, 8U);
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(finalize_set(&state, &request, &plan, true));
	assert(state.lock_state == PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITH_KEY);
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(finalize_set(&state, &request, &plan, true));
	assert(state.lock_state == PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED);

	state = supported();
	request = request_for(PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK, key,
		sizeof(key));
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) == 0U);
	assert(finalize_set(&state, &request, &plan, true));
	keyed = state;
	memcpy(wrong, key, sizeof(wrong));
	wrong[7] ^= 1U;
	request.data = wrong;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED);
	assert(plan.mutation_count == 1U && plan.mutations[0].value == 1U &&
		plan.projected_state.lock_state ==
			PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITHOUT_KEY);
	assert(finalize_set(&state, &request, &plan, false) ==
		PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_PUBLISHED_KEY_DESTROYED);
	assert(state.lock_state == PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITHOUT_KEY);
	state = keyed;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED);
	assert(finalize_set(&state, &request, &plan, true));
}

static void ready_to_boot(void)
{
	struct payload_mm_authvar_mor_state state = supported();
	struct payload_mm_authvar_mor_plan plan;

	/* Entry clear bit absent: a new request must survive this boot. */
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, true, attributes,
		1U, 1U, &plan) == 0U && !plan.mutation_count);
	assert(finalize_ready(&state, true, attributes, 1U, 1U, &plan, false));
	assert(state.ready_complete);

	state = supported();
	state.entry_clear_pending = true;
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, true, attributes,
		1U, 0xffU, &plan) == 0U && plan.mutation_count == 1U &&
		plan.mutations[0].value == 0xfeU);
	assert(finalize_ready(&state, true, attributes, 1U, 0xffU, &plan,
		false) ==
		PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_PUBLISHED_READY_CLEAR_FAILED);
	assert(state.ready_complete && !state.entry_clear_pending);
	state = supported();
	state.entry_clear_pending = true;
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, true, attributes,
		1U, 0xffU, &plan) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(finalize_ready(&state, true, attributes, 1U, 0xffU, &plan, true));
	assert(state.ready_complete && !state.entry_clear_pending);

	state = supported();
	state.entry_clear_pending = true;
	state.control_dirty = true;
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, true, attributes,
		1U, 1U, &plan) == 0U && !plan.mutation_count);
	assert(finalize_ready(&state, true, attributes, 1U, 1U, &plan, false));
	assert(state.ready_complete && state.control_dirty);

	state = supported();
	state.entry_clear_pending = true;
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, true, attributes,
		1U, 0U, &plan) == 0U && !plan.mutation_count);
	assert(finalize_ready(&state, true, attributes, 1U, 0U, &plan, false));

	state = supported();
	state.lock_state = PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITHOUT_KEY;
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, true, attributes,
		1U, 1U, &plan) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		plan.transition == PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_NOOP);
	state.entry_clear_pending = true;
	state.control_dirty = true;
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, true, attributes,
		1U, 1U, &plan) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		plan.transition == PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_NOOP);
	state.control_dirty = false;
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, true, attributes,
		1U, 0U, &plan) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		plan.transition == PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_NOOP);
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, true, attributes,
		1U, 1U, &plan) == PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED &&
		!plan.mutation_count &&
		plan.transition == PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_BLOCKED);
	assert(finalize_ready(&state, true, attributes, 1U, 1U, &plan, false));
	assert(state.ready_complete && !state.entry_clear_pending);
	state = supported();
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, false, 0U,
		0U, 0U, &plan) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		plan.transition == PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_NOOP);
}

static void boundaries(void)
{
	uint8_t value = 1U;
	struct payload_mm_authvar_mor_state state = supported();
	struct payload_mm_authvar_mor_request request = request_for(
		PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL, &value, sizeof(value));
	struct payload_mm_authvar_mor_plan plan;

	state.generation = UINT64_MAX - 1U;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) == 0U);
	assert(finalize_set(&state, &request, &plan, true));
	assert(state.generation == UINT64_MAX);
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	request.attributes = 0U;
	request.data_size = 0U;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	state = supported();
	assert(payload_mm_authvar_mor_set_plan(&state, &request,
		(struct payload_mm_authvar_mor_plan *)&state) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	state.generation = 0U;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	state = supported();
	state.lock_state = (enum payload_mm_authvar_mor_lock_state)-1;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	state = supported();
	state.supported = false;
	state.entry_clear_pending = true;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	state.entry_clear_pending = false;
	state.control_dirty = true;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
}

static void identity_and_integrity(void)
{
	uint8_t value = 1U;
	uint16_t near_name[ARRAY_SIZE(control_name)];
	struct payload_mm_authvar_mor_state state = supported();
	struct payload_mm_authvar_mor_state before;
	struct payload_mm_authvar_mor_boot_snapshot snapshot = {
		.phase = PAYLOAD_MM_AUTHVAR_MOR_INIT_END_OF_DXE,
		.trusted_platform_support = true,
	};
	struct payload_mm_authvar_mor_request request = request_for(
		PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL, &value, sizeof(value));
	struct payload_mm_authvar_mor_plan plan;
	struct payload_mm_authvar_mor_plan forged;

	assert(payload_mm_authvar_mor_init_plan(&state, &snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && !plan.transition);
	before = state;
	assert(finalize_init(&state, &snapshot, &plan, false));
	assert(!memcmp(&state, &before, sizeof(state)));

	memcpy(near_name, control_name, sizeof(near_name));
	near_name[0] ^= 1U;
	request.name = near_name;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && !plan.transition);
	assert(finalize_set(&state, &request, &plan, false));
	state.generation = UINT64_MAX;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		finalize_set(&state, &request, &plan, false));

	state = supported();
	state.ready_complete = true;
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, false, 0U, 0U,
		0U, &plan) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && !plan.transition);
	assert(finalize_ready(&state, false, 0U, 0U, 0U, &plan, false));

	state = supported();
	request = request_for(PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL, &value,
		sizeof(value));
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	forged = plan;
	forged.projected_state.supported = false;
	before = state;
	assert(!finalize_set(&state, &request, &forged, true));
	assert(!memcmp(&state, &before, sizeof(state)));
	forged = plan;
	forged.status = PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
	assert(!finalize_set(&state, &request, &forged, true));
	forged = plan;
	forged.transition = PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_LOCK_WRITE;
	assert(!finalize_set(&state, &request, &forged, true));
	forged = plan;
	forged.pass_to_store = false;
	assert(!finalize_set(&state, &request, &forged, true));
	forged = plan;
	forged.source_state.generation++;
	assert(!finalize_set(&state, &request, &forged, true));
	forged = plan;
	forged.projected_state.generation++;
	assert(!finalize_set(&state, &request, &forged, true));
	forged = plan;
	forged.mutation_count = 1U;
	assert(!finalize_set(&state, &request, &forged, true));
	assert(!finalize_set(&state, &request,
		(const struct payload_mm_authvar_mor_plan *)&request, true));

	state = supported();
	state.entry_clear_pending = true;
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, true, attributes,
		1U, 0xffU, &plan) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	forged = plan;
	forged.observed_control_value = 1U;
	assert(!finalize_ready(&state, true, attributes, 1U, 0xffU, &forged, true));
	forged = plan;
	forged.mutations[0].attributes = 0U;
	assert(!finalize_ready(&state, true, attributes, 1U, 0xffU, &forged, true));
	forged = plan;
	forged.mutations[0].value = 0U;
	assert(!finalize_ready(&state, true, attributes, 1U, 0xffU, &forged, true));
}

static void alignment_and_wrap(void)
{
	uint8_t state_bytes[sizeof(struct payload_mm_authvar_mor_state) + 8U];
	uint8_t snapshot_bytes[sizeof(struct payload_mm_authvar_mor_boot_snapshot) + 8U];
	uint8_t request_bytes[sizeof(struct payload_mm_authvar_mor_request) + 8U];
	uint8_t plan_bytes[sizeof(struct payload_mm_authvar_mor_plan) + 8U];
	uint8_t input_bytes[sizeof(struct payload_mm_authvar_mor_finalize_input) + 8U];
	struct payload_mm_authvar_mor_state state = supported();
	struct payload_mm_authvar_mor_boot_snapshot snapshot = { 0 };
	struct payload_mm_authvar_mor_plan plan;
	struct payload_mm_authvar_mor_plan untouched;
	struct payload_mm_authvar_mor_finalize_input input = {
		.kind = PAYLOAD_MM_AUTHVAR_MOR_INPUT_READY_TO_BOOT,
	};
	void *bad_state = (void *)((uintptr_t)state_bytes +
		(!((uintptr_t)state_bytes % _Alignof(struct payload_mm_authvar_mor_state))));
	void *bad_snapshot = (void *)((uintptr_t)snapshot_bytes +
		(!((uintptr_t)snapshot_bytes %
		 _Alignof(struct payload_mm_authvar_mor_boot_snapshot))));
	void *bad_request = (void *)((uintptr_t)request_bytes +
		(!((uintptr_t)request_bytes %
		 _Alignof(struct payload_mm_authvar_mor_request))));
	void *bad_plan = (void *)((uintptr_t)plan_bytes +
		(!((uintptr_t)plan_bytes % _Alignof(struct payload_mm_authvar_mor_plan))));
	void *bad_input = (void *)((uintptr_t)input_bytes +
		(!((uintptr_t)input_bytes %
		 _Alignof(struct payload_mm_authvar_mor_finalize_input))));

	memset(&plan, 0xa5, sizeof(plan));
	untouched = plan;
	assert(payload_mm_authvar_mor_init_plan(bad_state, &snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!memcmp(&plan, &untouched, sizeof(plan)));
	assert(payload_mm_authvar_mor_init_plan(&state, bad_snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_mor_set_plan(&state, bad_request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, false, 0U, 0U,
		0U, bad_plan) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_mor_finalize(&state, bad_input, &plan, false) ==
		PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_REJECTED);
	assert(payload_mm_authvar_mor_finalize(&state, &input, bad_plan, false) ==
		PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_REJECTED);
	assert(payload_mm_authvar_mor_ready_to_boot_plan(
		(const void *)(uintptr_t)(UINTPTR_MAX - 3U), false, 0U, 0U, 0U,
		&plan) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
}

static void oracle_matrix(void)
{
	uint8_t value = 2U;
	struct payload_mm_authvar_mor_state state = { 0 };
	struct payload_mm_authvar_mor_boot_snapshot snapshot = {
		.phase = PAYLOAD_MM_AUTHVAR_MOR_INIT_END_OF_DXE,
		.trusted_platform_support = true,
		.lock_present = true,
		.lock_attributes = attributes,
		.lock_size = 1U,
	};
	struct payload_mm_authvar_mor_request request;
	struct payload_mm_authvar_mor_plan plan;

	assert(payload_mm_authvar_mor_init_plan(&state, &snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && plan.mutation_count == 2U &&
		plan.mutations[0].variable == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL &&
		plan.mutations[1].variable == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK);
	snapshot.control_present = true;
	snapshot.control_attributes = attributes;
	snapshot.control_size = 2U;
	assert(payload_mm_authvar_mor_init_plan(&state, &snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && !plan.transition &&
		!plan.requires_durable_commit && !plan.mutation_count);
	snapshot.control_size = 1U;
	snapshot.lock_attributes = attributes ^ PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	assert(payload_mm_authvar_mor_init_plan(&state, &snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	snapshot.lock_attributes = attributes;
	snapshot.lock_value = 3U;
	assert(payload_mm_authvar_mor_init_plan(&state, &snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	snapshot.trusted_platform_support = false;
	snapshot.phase = PAYLOAD_MM_AUTHVAR_MOR_INIT_READY_TO_BOOT_FALLBACK;
	assert(payload_mm_authvar_mor_init_plan(&state, &snapshot, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && plan.mutation_count == 2U);

	state = supported();
	state.supported = false;
	request = request_for(PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK, &value, 1U);
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	value = 0U;
	request.attributes = 0U;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);
	request.attributes = attributes;
	request.data_size = 0U;
	assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);

	state = supported();
	state.lock_state = PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITHOUT_KEY;
	request.data_size = 1U;
	for (value = 0U; value <= 2U; value++) {
		assert(payload_mm_authvar_mor_set_plan(&state, &request, &plan) ==
			PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED);
	}
	state = supported();
	state.entry_clear_pending = true;
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, false, 0U, 0U,
		0U, &plan) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(payload_mm_authvar_mor_ready_to_boot_plan(&state, true, attributes,
		2U, 1U, &plan) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
}

int main(void)
{
	classifier();
	initialization();
	control();
	lock();
	ready_to_boot();
	boundaries();
	identity_and_integrity();
	alignment_and_wrap();
	oracle_matrix();
	return 0;
}
