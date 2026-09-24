/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor.h>
#include <stdint.h>
#include <string.h>

#define MOR_ATTRIBUTES (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE | \
	PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS | \
	PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)

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

static bool range_valid(const void *pointer, size_t size)
{
	return !size || (pointer &&
		(uintptr_t)pointer <= UINTPTR_MAX - (size - 1U));
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_address = (uintptr_t)left;
	uintptr_t right_address = (uintptr_t)right;

	if (!range_valid(left, left_size) || !range_valid(right, right_size))
		return true;
	if (!left_size || !right_size)
		return false;
	if (left_address <= right_address)
		return right_address - left_address < left_size;
	return left_address - right_address < right_size;
}

static bool object_valid(const void *pointer, size_t size, size_t alignment)
{
	return range_valid(pointer, size) &&
		!((uintptr_t)pointer % alignment);
}

static bool state_valid(const struct payload_mm_authvar_mor_state *state)
{
	uint8_t combined = 0U;

	if (!object_valid(state, sizeof(*state), _Alignof(*state)) ||
	    state->lock_state < PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED ||
	    state->lock_state > PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITH_KEY ||
	    (state->initialized && !state->generation) ||
	    (state->ready_complete && state->entry_clear_pending))
		return false;
	for (size_t i = 0U; i < sizeof(state->key); i++)
		combined |= state->key[i];
	if (!state->initialized)
		return !state->generation && !state->supported &&
			state->lock_state == PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED &&
			!state->entry_clear_pending && !state->control_dirty &&
			!state->ready_complete && !combined;
	if (!state->supported &&
	    (state->lock_state != PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED ||
	     state->entry_clear_pending || state->control_dirty))
		return false;
	if (state->lock_state == PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITH_KEY)
		return state->supported;
	return !combined;
}

enum payload_mm_authvar_mor_variable payload_mm_authvar_mor_classify(
	const uint8_t vendor_guid[16], const void *name, size_t name_size)
{
	if (!range_valid(vendor_guid, 16U) || !range_valid(name, name_size))
		return PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE;
	if (name_size == sizeof(control_name) &&
	    !memcmp(vendor_guid, control_guid, sizeof(control_guid)) &&
	    !memcmp(name, control_name, sizeof(control_name)))
		return PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL;
	if (name_size == sizeof(lock_name) &&
	    !memcmp(vendor_guid, lock_guid, sizeof(lock_guid)) &&
	    !memcmp(name, lock_name, sizeof(lock_name)))
		return PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK;
	return PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE;
}

static void begin_plan(const struct payload_mm_authvar_mor_state *state,
	struct payload_mm_authvar_mor_plan *plan,
	enum payload_mm_authvar_mor_transition transition)
{
	plan->status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	plan->transition = transition;
	plan->source_state = *state;
	plan->projected_state = *state;
	plan->projected_state.generation++;
}

static void mutation(struct payload_mm_authvar_mor_plan *plan,
	enum payload_mm_authvar_mor_mutation_kind kind,
	enum payload_mm_authvar_mor_variable variable, uint8_t value)
{
	struct payload_mm_authvar_mor_mutation *target =
		&plan->mutations[plan->mutation_count++];

	target->kind = kind;
	target->variable = variable;
	target->attributes = kind == PAYLOAD_MM_AUTHVAR_MOR_MUTATION_WRITE ?
		MOR_ATTRIBUTES : 0U;
	target->value = value;
}

static bool output_valid(const void *input, size_t input_size,
	const struct payload_mm_authvar_mor_plan *plan)
{
	return object_valid(plan, sizeof(*plan), _Alignof(*plan)) &&
		!ranges_overlap(input, input_size, plan, sizeof(*plan));
}

static bool key_equal(const uint8_t *left, const uint8_t *right)
{
	uint8_t difference = 0U;

	for (size_t i = 0U; i < PAYLOAD_MM_AUTHVAR_MOR_KEY_SIZE; i++)
		difference |= left[i] ^ right[i];
	return !difference;
}

static bool finalize_set_ranges_valid(
	const struct payload_mm_authvar_mor_state *state,
	const struct payload_mm_authvar_mor_finalize_input *input,
	const struct payload_mm_authvar_mor_plan *plan)
{
	const struct payload_mm_authvar_mor_request *request = input->set;

	if (!object_valid(request, sizeof(*request), _Alignof(*request)) ||
	    !range_valid(request->name, request->name_size) ||
	    !range_valid(request->data, request->data_size))
		return false;
	return !ranges_overlap(input, sizeof(*input), request, sizeof(*request)) &&
		!ranges_overlap(input, sizeof(*input), request->name,
			request->name_size) &&
		!ranges_overlap(input, sizeof(*input), request->data,
			request->data_size) &&
		!ranges_overlap(state, sizeof(*state), request, sizeof(*request)) &&
		!ranges_overlap(state, sizeof(*state), request->name,
			request->name_size) &&
		!ranges_overlap(state, sizeof(*state), request->data,
			request->data_size) &&
		!ranges_overlap(plan, sizeof(*plan), request, sizeof(*request)) &&
		!ranges_overlap(plan, sizeof(*plan), request->name,
			request->name_size) &&
		!ranges_overlap(plan, sizeof(*plan), request->data,
			request->data_size);
}

uint64_t payload_mm_authvar_mor_init_plan(
	const struct payload_mm_authvar_mor_state *state,
	const struct payload_mm_authvar_mor_boot_snapshot *snapshot,
	struct payload_mm_authvar_mor_plan *plan)
{
	if (!object_valid(state, sizeof(*state), _Alignof(*state)) ||
	    !object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)) ||
	    !output_valid(state, sizeof(*state), plan) ||
	    ranges_overlap(state, sizeof(*state), snapshot, sizeof(*snapshot)) ||
	    ranges_overlap(snapshot, sizeof(*snapshot), plan, sizeof(*plan)))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	memset(plan, 0, sizeof(*plan));
	if (!state_valid(state))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (snapshot->phase < PAYLOAD_MM_AUTHVAR_MOR_INIT_END_OF_DXE ||
	    snapshot->phase > PAYLOAD_MM_AUTHVAR_MOR_INIT_READY_TO_BOOT_FALLBACK)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (state->initialized) {
		plan->status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		plan->source_state = *state;
		plan->projected_state = *state;
		return plan->status;
	}
	if (snapshot->trusted_platform_support &&
	    ((snapshot->control_present &&
	      (snapshot->control_attributes != MOR_ATTRIBUTES ||
	       snapshot->control_size != 1U)) ||
	     (snapshot->lock_present &&
	      (snapshot->lock_attributes != MOR_ATTRIBUTES ||
	       snapshot->lock_size != 1U || snapshot->lock_value > 2U)))) {
		plan->status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		return plan->status;
	}
	begin_plan(state, plan, snapshot->trusted_platform_support ?
		PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_INIT_SUPPORTED :
		PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_INIT_UNSUPPORTED);
	plan->requires_durable_commit = true;
	plan->projected_state.initialized = true;
	if (!snapshot->trusted_platform_support) {
		mutation(plan, PAYLOAD_MM_AUTHVAR_MOR_MUTATION_DELETE,
			PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL, 0U);
		mutation(plan, PAYLOAD_MM_AUTHVAR_MOR_MUTATION_DELETE,
			PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK, 0U);
		return plan->status;
	}
	plan->projected_state.supported = true;
	plan->projected_state.entry_clear_pending =
		snapshot->entry_control_present && !!(snapshot->entry_control_value & 1U);
	plan->projected_state.control_dirty = snapshot->control_dirty_since_entry;
	if (!snapshot->control_present)
		mutation(plan, PAYLOAD_MM_AUTHVAR_MOR_MUTATION_WRITE,
			PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL, 0U);
	mutation(plan, PAYLOAD_MM_AUTHVAR_MOR_MUTATION_WRITE,
		PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK, 0U);
	return plan->status;
}

uint64_t payload_mm_authvar_mor_set_plan(
	const struct payload_mm_authvar_mor_state *state,
	const struct payload_mm_authvar_mor_request *request,
	struct payload_mm_authvar_mor_plan *plan)
{
	enum payload_mm_authvar_mor_variable variable;
	const uint8_t *data;

	if (!object_valid(state, sizeof(*state), _Alignof(*state)) ||
	    !object_valid(request, sizeof(*request), _Alignof(*request)) ||
	    !range_valid(request->name, request->name_size) ||
	    !range_valid(request->data, request->data_size) ||
	    !output_valid(state, sizeof(*state), plan) ||
	    ranges_overlap(state, sizeof(*state), request, sizeof(*request)) ||
	    ranges_overlap(state, sizeof(*state), request->name, request->name_size) ||
	    ranges_overlap(state, sizeof(*state), request->data, request->data_size) ||
	    ranges_overlap(request, sizeof(*request), plan, sizeof(*plan)) ||
	    ranges_overlap(request, sizeof(*request), request->name,
		request->name_size) ||
	    ranges_overlap(request, sizeof(*request), request->data,
		request->data_size) ||
	    ranges_overlap(request->name, request->name_size, request->data,
		request->data_size) ||
	    ranges_overlap(request->name, request->name_size, plan, sizeof(*plan)) ||
	    ranges_overlap(request->data, request->data_size, plan, sizeof(*plan)))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	memset(plan, 0, sizeof(*plan));
	if (!state_valid(state) || !state->initialized)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	variable = payload_mm_authvar_mor_classify(request->vendor_guid,
		request->name, request->name_size);
	plan->status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	plan->source_state = *state;
	plan->projected_state = *state;
	if (variable == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE)
		return plan->status;
	plan->matched = true;
	if (variable == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL) {
		if (request->attributes != MOR_ATTRIBUTES || request->data_size != 1U) {
			plan->status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
			return plan->status;
		}
	} else {
		if (!request->attributes || !request->data_size) {
			plan->status = PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
			return plan->status;
		}
		if (request->attributes != MOR_ATTRIBUTES ||
		    (request->data_size != 1U &&
		     request->data_size != PAYLOAD_MM_AUTHVAR_MOR_KEY_SIZE)) {
			plan->status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
			return plan->status;
		}
	}
	data = request->data;
	if (variable == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK &&
	    state->lock_state == PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED &&
	    request->data_size == 1U && data[0] > 1U) {
		plan->status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		return plan->status;
	}
	if (!state->supported) {
		plan->status = PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
		return plan->status;
	}
	if (variable == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL) {
		if (state->lock_state != PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED) {
			plan->status = PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
			return plan->status;
		}
		if (state->generation == UINT64_MAX) {
			plan->status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
			return plan->status;
		}
		begin_plan(state, plan, PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_CONTROL_WRITE);
		plan->matched = plan->pass_to_store = plan->requires_durable_commit = true;
		plan->projected_state.control_dirty = true;
		return plan->status;
	}
	if (state->lock_state == PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED) {
		if (state->generation == UINT64_MAX) {
			plan->status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
			return plan->status;
		}
		begin_plan(state, plan, request->data_size == 1U ?
			PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_LOCK_WRITE :
			PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_LOCK_KEY);
		plan->matched = plan->requires_durable_commit = true;
		if (request->data_size == 1U) {
			if (data[0] == 1U)
				plan->projected_state.lock_state =
					PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITHOUT_KEY;
			mutation(plan, PAYLOAD_MM_AUTHVAR_MOR_MUTATION_WRITE, variable, data[0]);
			return plan->status;
		}
		memcpy(plan->projected_state.key, data, sizeof(plan->projected_state.key));
		memcpy(plan->transition_key, data, sizeof(plan->transition_key));
		plan->projected_state.lock_state = PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITH_KEY;
		mutation(plan, PAYLOAD_MM_AUTHVAR_MOR_MUTATION_WRITE, variable, 2U);
		return plan->status;
	}
	if (state->lock_state != PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITH_KEY ||
	    request->data_size != PAYLOAD_MM_AUTHVAR_MOR_KEY_SIZE) {
		plan->status = PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
		return plan->status;
	}
	if (state->generation == UINT64_MAX) {
		plan->status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		return plan->status;
	}
	begin_plan(state, plan, key_equal(data, state->key) ?
		PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_UNLOCK_KEY :
		PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_KEY_MISMATCH);
	plan->matched = plan->requires_durable_commit = true;
	memcpy(plan->transition_key, data, sizeof(plan->transition_key));
	memset(plan->projected_state.key, 0, sizeof(plan->projected_state.key));
	if (plan->transition == PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_UNLOCK_KEY) {
		plan->projected_state.lock_state = PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED;
		mutation(plan, PAYLOAD_MM_AUTHVAR_MOR_MUTATION_WRITE, variable, 0U);
		return plan->status;
	}
	plan->projected_state.lock_state = PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITHOUT_KEY;
	mutation(plan, PAYLOAD_MM_AUTHVAR_MOR_MUTATION_WRITE, variable, 1U);
	plan->status = PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
	return plan->status;
}

uint64_t payload_mm_authvar_mor_ready_to_boot_plan(
	const struct payload_mm_authvar_mor_state *state, bool control_present,
	uint32_t control_attributes, size_t control_size, uint8_t control_value,
	struct payload_mm_authvar_mor_plan *plan)
{
	if (!object_valid(state, sizeof(*state), _Alignof(*state)) ||
	    !output_valid(state, sizeof(*state), plan))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	memset(plan, 0, sizeof(*plan));
	if (!state_valid(state) || !state->initialized)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (state->ready_complete) {
		plan->status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		plan->source_state = *state;
		plan->projected_state = *state;
		return plan->status;
	}
	if (state->generation == UINT64_MAX) {
		plan->status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		return plan->status;
	}
	if (!state->supported || !state->entry_clear_pending || state->control_dirty) {
		begin_plan(state, plan, PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_NOOP);
		plan->projected_state.entry_clear_pending = false;
		plan->projected_state.ready_complete = true;
		return plan->status;
	}
	if (!control_present || control_attributes != MOR_ATTRIBUTES ||
	    control_size != 1U) {
		plan->status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		return plan->status;
	}
	if (!(control_value & 1U)) {
		begin_plan(state, plan, PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_NOOP);
		plan->observed_control_valid = true;
		plan->observed_control_value = control_value;
		plan->projected_state.entry_clear_pending = false;
		plan->projected_state.ready_complete = true;
		return plan->status;
	}
	if (state->lock_state != PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED) {
		begin_plan(state, plan,
			PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_BLOCKED);
		plan->status = PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED;
		plan->observed_control_valid = true;
		plan->observed_control_value = control_value;
		plan->projected_state.entry_clear_pending = false;
		plan->projected_state.ready_complete = true;
		return plan->status;
	}
	begin_plan(state, plan,
		PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_CLEAR);
	plan->observed_control_valid = true;
	plan->observed_control_value = control_value;
	plan->projected_state.entry_clear_pending = false;
	plan->projected_state.ready_complete = true;
	if (plan->transition == PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_CLEAR) {
		mutation(plan, PAYLOAD_MM_AUTHVAR_MOR_MUTATION_WRITE,
			PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL,
			control_value & (uint8_t)~1U);
		plan->requires_durable_commit = true;
	}
	return plan->status;
}

static bool write_is(const struct payload_mm_authvar_mor_plan *plan,
	size_t index, enum payload_mm_authvar_mor_variable variable, uint8_t value)
{
	return index < plan->mutation_count &&
		plan->mutations[index].kind == PAYLOAD_MM_AUTHVAR_MOR_MUTATION_WRITE &&
		plan->mutations[index].variable == variable &&
		plan->mutations[index].attributes == MOR_ATTRIBUTES &&
		plan->mutations[index].value == value;
}

static bool delete_is(const struct payload_mm_authvar_mor_plan *plan,
	size_t index, enum payload_mm_authvar_mor_variable variable)
{
	return index < plan->mutation_count &&
		plan->mutations[index].kind == PAYLOAD_MM_AUTHVAR_MOR_MUTATION_DELETE &&
		plan->mutations[index].variable == variable &&
		!plan->mutations[index].attributes && !plan->mutations[index].value;
}

static bool transition_valid(const struct payload_mm_authvar_mor_plan *plan)
{
	struct payload_mm_authvar_mor_state expected;
	static const struct payload_mm_authvar_mor_mutation empty;
	bool shape;
	uint8_t key_bytes = 0U;

	if (!state_valid(&plan->source_state) ||
	    !state_valid(&plan->projected_state) ||
	    plan->mutation_count > PAYLOAD_MM_AUTHVAR_MOR_MAX_MUTATIONS)
		return false;
	for (size_t i = plan->mutation_count;
	     i < PAYLOAD_MM_AUTHVAR_MOR_MAX_MUTATIONS; i++)
		if (memcmp(&plan->mutations[i], &empty, sizeof(empty)))
			return false;
	for (size_t i = 0U; i < sizeof(plan->transition_key); i++)
		key_bytes |= plan->transition_key[i];
	if (plan->transition == PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_NONE)
		return plan->status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
			!plan->matched && !plan->pass_to_store &&
			!plan->requires_durable_commit && !plan->mutation_count &&
			!plan->observed_control_valid &&
			!plan->observed_control_value && !key_bytes &&
			!memcmp(&plan->source_state, &plan->projected_state,
				sizeof(plan->source_state));
	if (plan->source_state.generation == UINT64_MAX ||
	    plan->projected_state.generation != plan->source_state.generation + 1U)
		return false;
	expected = plan->source_state;
	expected.generation++;
	switch (plan->transition) {
	case PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_INIT_SUPPORTED:
		expected.initialized = expected.supported = true;
		expected.entry_clear_pending = plan->projected_state.entry_clear_pending;
		expected.control_dirty = plan->projected_state.control_dirty;
		shape = !key_bytes && !plan->matched && !plan->pass_to_store &&
			!plan->source_state.initialized &&
			plan->requires_durable_commit &&
			(plan->mutation_count == 1U || plan->mutation_count == 2U) &&
			write_is(plan, plan->mutation_count - 1U,
				PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK, 0U) &&
			(plan->mutation_count == 1U || write_is(plan, 0U,
				PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL, 0U));
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_INIT_UNSUPPORTED:
		expected.initialized = true;
		shape = !key_bytes && !plan->matched && !plan->pass_to_store &&
			!plan->source_state.initialized &&
			plan->requires_durable_commit && plan->mutation_count == 2U &&
			delete_is(plan, 0U, PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL) &&
			delete_is(plan, 1U, PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK);
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_CONTROL_WRITE:
		expected.control_dirty = true;
		shape = !key_bytes && plan->matched && plan->pass_to_store &&
			plan->requires_durable_commit && !plan->mutation_count &&
			plan->source_state.supported &&
			plan->source_state.lock_state == PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED;
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_LOCK_WRITE:
		expected.lock_state = plan->mutations[0].value ?
			PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITHOUT_KEY :
			PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED;
		shape = !key_bytes && plan->matched && !plan->pass_to_store &&
			plan->requires_durable_commit &&
			plan->source_state.supported &&
			plan->source_state.lock_state == PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED &&
			plan->mutation_count == 1U &&
			write_is(plan, 0U, PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK,
				plan->mutations[0].value) && plan->mutations[0].value <= 1U;
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_LOCK_KEY:
		expected.lock_state = PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITH_KEY;
		memcpy(expected.key, plan->transition_key, sizeof(expected.key));
		shape = plan->matched && !plan->pass_to_store &&
			plan->requires_durable_commit &&
			plan->source_state.supported &&
			plan->source_state.lock_state == PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED &&
			plan->mutation_count == 1U && write_is(plan, 0U,
				PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK, 2U);
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_UNLOCK_KEY:
		expected.lock_state = PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED;
		memset(expected.key, 0, sizeof(expected.key));
		shape = plan->matched && !plan->pass_to_store &&
			plan->requires_durable_commit &&
			plan->source_state.supported &&
			plan->source_state.lock_state == PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITH_KEY &&
			key_equal(plan->transition_key, plan->source_state.key) &&
			plan->mutation_count == 1U && write_is(plan, 0U,
				PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK, 0U);
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_KEY_MISMATCH:
		expected.lock_state = PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITHOUT_KEY;
		memset(expected.key, 0, sizeof(expected.key));
		shape = plan->matched && !plan->pass_to_store &&
			plan->requires_durable_commit &&
			plan->source_state.supported &&
			plan->status == PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED &&
			plan->source_state.lock_state == PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITH_KEY &&
			!key_equal(plan->transition_key, plan->source_state.key) &&
			plan->mutation_count == 1U && write_is(plan, 0U,
				PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK, 1U);
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_CLEAR:
		expected.entry_clear_pending = false;
		expected.ready_complete = true;
		shape = !key_bytes && !plan->matched && !plan->pass_to_store &&
			plan->observed_control_valid &&
			(plan->observed_control_value & 1U) &&
			plan->requires_durable_commit && plan->mutation_count == 1U &&
			plan->source_state.supported && !plan->source_state.ready_complete &&
			plan->source_state.lock_state == PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED &&
			plan->source_state.entry_clear_pending &&
			!plan->source_state.control_dirty && write_is(plan, 0U,
				PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL,
				plan->observed_control_value & (uint8_t)~1U);
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_BLOCKED:
		expected.entry_clear_pending = false;
		expected.ready_complete = true;
		shape = !key_bytes && !plan->matched && !plan->pass_to_store &&
			plan->observed_control_valid &&
			(plan->observed_control_value & 1U) &&
			!plan->requires_durable_commit && !plan->mutation_count &&
			plan->source_state.supported && !plan->source_state.ready_complete &&
			plan->source_state.lock_state != PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED &&
			plan->source_state.entry_clear_pending &&
			!plan->source_state.control_dirty;
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_NOOP:
		expected.entry_clear_pending = false;
		expected.ready_complete = true;
		shape = !key_bytes && !plan->matched && !plan->pass_to_store &&
			!plan->requires_durable_commit && !plan->mutation_count &&
			(!plan->source_state.supported ||
			 (!plan->source_state.entry_clear_pending ||
			  plan->source_state.control_dirty ||
			  (plan->observed_control_valid &&
			   !(plan->observed_control_value & 1U))));
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_NONE:
	default:
		return false;
	}
	return shape && plan->status == ((plan->transition ==
		PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_KEY_MISMATCH ||
		plan->transition == PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_BLOCKED) ?
		PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED :
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) &&
		!memcmp(&expected, &plan->projected_state, sizeof(expected));
}

enum payload_mm_authvar_mor_finalize_result payload_mm_authvar_mor_finalize(
	struct payload_mm_authvar_mor_state *state,
	const struct payload_mm_authvar_mor_finalize_input *input,
	const struct payload_mm_authvar_mor_plan *plan, bool durable_commit)
{
	struct payload_mm_authvar_mor_plan expected;
	uint64_t status;

	if (!state_valid(state) ||
	    !object_valid(input, sizeof(*input), _Alignof(*input)) ||
	    !object_valid(plan, sizeof(*plan), _Alignof(*plan)) ||
	    ranges_overlap(state, sizeof(*state), input, sizeof(*input)) ||
	    ranges_overlap(state, sizeof(*state), plan, sizeof(*plan)) ||
	    ranges_overlap(input, sizeof(*input), plan, sizeof(*plan)))
		return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_REJECTED;
	switch (input->kind) {
	case PAYLOAD_MM_AUTHVAR_MOR_INPUT_INIT:
		status = payload_mm_authvar_mor_init_plan(state, &input->init,
			&expected);
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_INPUT_SET:
		if (!finalize_set_ranges_valid(state, input, plan))
			return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_REJECTED;
		status = payload_mm_authvar_mor_set_plan(state, input->set, &expected);
		break;
	case PAYLOAD_MM_AUTHVAR_MOR_INPUT_READY_TO_BOOT:
		status = payload_mm_authvar_mor_ready_to_boot_plan(state,
			input->ready.control_present, input->ready.control_attributes,
			input->ready.control_size, input->ready.control_value, &expected);
		break;
	default:
		return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_REJECTED;
	}
	if (status != plan->status || memcmp(&expected, plan, sizeof(expected)) ||
	    !transition_valid(&expected))
		return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_REJECTED;
	if (expected.requires_durable_commit && !durable_commit) {
		if (expected.transition == PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_KEY_MISMATCH) {
			*state = expected.projected_state;
			return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_PUBLISHED_KEY_DESTROYED;
		}
		if (expected.transition == PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_CLEAR) {
			*state = expected.projected_state;
			return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_PUBLISHED_READY_CLEAR_FAILED;
		}
		return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_REJECTED;
	}
	*state = expected.projected_state;
	return PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_PUBLISHED;
}
