/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/capsule_anchor_policy.h>
#include <security/tpm/tss.h>
#include <security/tpm/tss2_policy.h>
#include <string.h>

#if ENV_SMM && !ENV_TEST
#error "The capsule TPM policy provider must not be built in SMM"
#endif

#define PROVIDER_READY 0x43505231U
#define PROVIDER_INITIALIZING 0x43505230U
#define PROVIDER_PREPARING 0x43505232U
#define PROVIDER_RUNNING 0x43505233U
#define PROVIDER_INSTALLING 0x43505234U
#define PROVIDER_FINISHED 0x43505235U
#define PROVIDER_FAILED 0x43505236U
#define PROVIDER_READING 0x43505237U
#define PROVIDER_OPERATING 0x43505238U
#define TPM2_CC_POLICY_AUTHORIZE 0x0000016aU
#define TPM2_CC_POLICY_COMMAND_CODE 0x0000016cU
#define TPM2_CC_POLICY_OR 0x00000171U

struct buffer {
	uint8_t *data;
	size_t capacity;
	size_t size;
};

struct operation_snapshot {
	struct capsule_tpm_anchor_binding binding;
	struct capsule_tpm_anchor_value current;
	struct capsule_tpm_anchor_value candidate;
	uint8_t authority_name[CAPSULE_TPM_ANCHOR_AUTHORITY_NAME_SIZE];
	uint8_t policy_ref[CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE];
	uint16_t policy_ref_size;
	struct capsule_tpm_anchor_policy_authorization policy;
};

struct operation_context {
	const struct tlcl2_transport *transport;
	const struct tlcl2_policy_session *session;
	const struct capsule_tpm_anchor_value *old_value;
	struct operation_snapshot input;
	struct tlcl2_rsa_public_key key;
	uint8_t branches[2 * CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	uint8_t cp_hash[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	uint8_t authorization_hash[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	uint8_t input_hash[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	bool command_attempted;
	bool cleanup_failed;
	tpm_result_t command_result;
	bool write;
};

_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
	"capsule TPM policy provider control must be lock-free");
_Static_assert(CONFIG_STACK_SIZE >= 0x3000,
	"capsule TPM policy provider requires a 12 KiB ramstage stack");

static void secure_clear(void *data, size_t size)
{
	volatile uint8_t *byte = data;

	while (size--)
		*byte++ = 0;
}

static bool append(struct buffer *buffer, const void *data, size_t size)
{
	if ((size && !data) || size > buffer->capacity - buffer->size)
		return false;
	memcpy(buffer->data + buffer->size, data, size);
	buffer->size += size;
	return true;
}

static bool append_be16(struct buffer *buffer, uint16_t value)
{
	const uint8_t bytes[] = { value >> 8, value };

	return append(buffer, bytes, sizeof(bytes));
}

static bool append_be32(struct buffer *buffer, uint32_t value)
{
	const uint8_t bytes[] = {
		value >> 24, value >> 16, value >> 8, value,
	};

	return append(buffer, bytes, sizeof(bytes));
}

static bool sha256(const void *data, size_t size,
	uint8_t digest[CAPSULE_TPM_ANCHOR_POLICY_SIZE])
{
	struct vb2_hash hash;

	if (vb2_hash_calculate(false, data, size, VB2_HASH_SHA256, &hash) !=
		VB2_SUCCESS)
		return false;
	memcpy(digest, hash.raw, CAPSULE_TPM_ANCHOR_POLICY_SIZE);
	secure_clear(&hash, sizeof(hash));
	return true;
}

static bool bytes_zero(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	uint8_t value = 0;

	for (size_t i = 0; i < size; i++)
		value |= bytes[i];
	return value == 0;
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_start = (uintptr_t)left;
	uintptr_t right_start = (uintptr_t)right;

	if (!left_size || !right_size)
		return false;
	if (!left || !right || left_size > UINTPTR_MAX - left_start ||
	    right_size > UINTPTR_MAX - right_start)
		return true;
	return left_start < right_start + right_size &&
		right_start < left_start + left_size;
}

static bool binding_valid(const struct capsule_tpm_anchor_binding *binding)
{
	return binding &&
		binding->policy_revision == CAPSULE_TPM_ANCHOR_POLICY_REVISION &&
		(binding->nv_index & 0xff000000U) == HR_NV_INDEX &&
		binding->authority_name[0] == (TPM_ALG_SHA256 >> 8) &&
		binding->authority_name[1] == (TPM_ALG_SHA256 & 0xff) &&
		!bytes_zero(binding->authority_name + sizeof(uint16_t),
			CAPSULE_TPM_ANCHOR_POLICY_SIZE) &&
		binding->policy_ref_size &&
		binding->policy_ref_size <= CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE &&
		bytes_zero(binding->policy_ref + binding->policy_ref_size,
			sizeof(binding->policy_ref) - binding->policy_ref_size) &&
		binding->write_locked <= 1 &&
		bytes_zero(binding->reserved, sizeof(binding->reserved));
}

static bool policy_authorization_valid(
	const struct capsule_tpm_anchor_policy_authorization *policy,
	size_t modulus_size)
{
	return !bytes_zero(policy->approved_policy,
			sizeof(policy->approved_policy)) &&
		!bytes_zero(policy->cp_hash, sizeof(policy->cp_hash)) &&
		!bytes_zero(policy->nonce, sizeof(policy->nonce)) &&
		policy->signature_size == modulus_size &&
		bytes_zero(policy->reserved, sizeof(policy->reserved)) &&
		!bytes_zero(policy->signature, policy->signature_size) &&
		bytes_zero(policy->signature + policy->signature_size,
			sizeof(policy->signature) - policy->signature_size) &&
		!policy->reserved2;
}

static bool value_valid(const struct capsule_tpm_anchor_value *value)
{
	return value->epoch &&
		!bytes_zero(value->digest, sizeof(value->digest));
}

static bool authorization_valid(
	const struct capsule_tpm_anchor_authorization *authorization,
	const struct capsule_tpm_anchor_binding *binding)
{
	bool modulus_valid;

	if (!authorization || !binding_valid(binding))
		return false;
	modulus_valid = authorization->modulus_size == 256 ||
		authorization->modulus_size == 384 ||
		authorization->modulus_size == 512;
	return authorization->revision ==
			CAPSULE_TPM_ANCHOR_AUTHORIZATION_REVISION &&
		authorization->size == sizeof(*authorization) &&
		authorization->policy_revision == binding->policy_revision &&
		authorization->nv_index == binding->nv_index &&
		authorization->generation && authorization->transaction &&
		value_valid(&authorization->current) &&
		value_valid(&authorization->candidate) &&
		authorization->current.epoch != UINT64_MAX &&
		authorization->candidate.epoch ==
			authorization->current.epoch + 1 &&
		memcmp(authorization->current.digest,
			authorization->candidate.digest,
			sizeof(authorization->current.digest)) &&
		!memcmp(authorization->authority_name, binding->authority_name,
			sizeof(authorization->authority_name)) &&
		authorization->policy_ref_size == binding->policy_ref_size &&
		!memcmp(authorization->policy_ref, binding->policy_ref,
			sizeof(authorization->policy_ref)) && modulus_valid &&
		!authorization->reserved &&
		!bytes_zero(authorization->modulus,
			authorization->modulus_size) &&
		bytes_zero(authorization->modulus + authorization->modulus_size,
			sizeof(authorization->modulus) -
			authorization->modulus_size) &&
		!authorization->reserved2 &&
		policy_authorization_valid(&authorization->write,
			authorization->modulus_size) &&
		policy_authorization_valid(&authorization->lock,
			authorization->modulus_size) &&
		memcmp(authorization->write.approved_policy,
			authorization->lock.approved_policy,
			sizeof(authorization->write.approved_policy)) &&
		memcmp(authorization->write.cp_hash,
			authorization->lock.cp_hash,
			sizeof(authorization->write.cp_hash));
}

static bool hash_parts(const void *const *parts, const size_t *sizes,
	size_t count, uint8_t digest[CAPSULE_TPM_ANCHOR_POLICY_SIZE])
{
	uint8_t bytes[192];
	struct buffer buffer = { .data = bytes, .capacity = sizeof(bytes) };
	bool valid = true;

	for (size_t i = 0; i < count; i++)
		valid &= append(&buffer, parts[i], sizes[i]);
	valid &= sha256(buffer.data, buffer.size, digest);
	secure_clear(bytes, sizeof(bytes));
	return valid;
}

static bool policy_update(const uint8_t old[CAPSULE_TPM_ANCHOR_POLICY_SIZE],
	uint32_t code, const void *argument, size_t argument_size,
	uint8_t digest[CAPSULE_TPM_ANCHOR_POLICY_SIZE])
{
	const uint8_t command[] = {
		code >> 24, code >> 16, code >> 8, code,
	};
	const void *parts[] = { old, command, argument };
	const size_t sizes[] = { CAPSULE_TPM_ANCHOR_POLICY_SIZE, sizeof(command),
		argument_size };

	return hash_parts(parts, sizes, ARRAY_SIZE(parts), digest);
}

static bool make_static_policy(
	const struct capsule_tpm_anchor_binding *binding,
	uint8_t policy[CAPSULE_TPM_ANCHOR_POLICY_SIZE],
	uint8_t branches[2 * CAPSULE_TPM_ANCHOR_POLICY_SIZE])
{
	const uint8_t zero[CAPSULE_TPM_ANCHOR_POLICY_SIZE] = { 0 };
	uint8_t authorized[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	uint8_t command[4];
	uint8_t write[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	uint8_t lock[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	const void *parts[] = { authorized, binding->policy_ref };
	const size_t sizes[] = { sizeof(authorized), binding->policy_ref_size };
	bool valid = false;

	if (!policy_update(zero, TPM2_CC_POLICY_AUTHORIZE,
		binding->authority_name, sizeof(binding->authority_name), authorized) ||
	    !hash_parts(parts, sizes, ARRAY_SIZE(parts), authorized))
		goto out;
	command[0] = TPM2_NV_Write >> 24;
	command[1] = TPM2_NV_Write >> 16;
	command[2] = TPM2_NV_Write >> 8;
	command[3] = (uint8_t)TPM2_NV_Write;
	if (!policy_update(authorized, TPM2_CC_POLICY_COMMAND_CODE, command,
		sizeof(command), write))
		goto out;
	command[0] = TPM2_NV_WriteLock >> 24;
	command[1] = TPM2_NV_WriteLock >> 16;
	command[2] = TPM2_NV_WriteLock >> 8;
	command[3] = (uint8_t)TPM2_NV_WriteLock;
	if (!policy_update(authorized, TPM2_CC_POLICY_COMMAND_CODE, command,
		sizeof(command), lock))
		goto out;
	if (memcmp(write, lock, sizeof(write)) < 0) {
		memcpy(branches, write, sizeof(write));
		memcpy(branches + sizeof(write), lock, sizeof(lock));
	} else {
		memcpy(branches, lock, sizeof(lock));
		memcpy(branches + sizeof(lock), write, sizeof(write));
	}
	valid = policy_update(zero, TPM2_CC_POLICY_OR, branches,
		2 * CAPSULE_TPM_ANCHOR_POLICY_SIZE, policy);
out:
	secure_clear(authorized, sizeof(authorized));
	secure_clear(command, sizeof(command));
	secure_clear(write, sizeof(write));
	secure_clear(lock, sizeof(lock));
	return valid;
}

static bool make_nv_name(const struct capsule_tpm_anchor_binding *binding,
	const uint8_t policy[CAPSULE_TPM_ANCHOR_POLICY_SIZE],
	uint8_t name[CAPSULE_TPM_ANCHOR_AUTHORITY_NAME_SIZE])
{
	uint8_t public_area[46];
	struct buffer buffer = {
		.data = public_area,
		.capacity = sizeof(public_area),
	};
	bool valid;

	valid = append_be32(&buffer, binding->nv_index) &&
		append_be16(&buffer, TPM_ALG_SHA256) &&
		append_be32(&buffer, CAPSULE_TPM_ANCHOR_ATTRIBUTES |
			CAPSULE_TPM_ANCHOR_WRITTEN |
			(binding->write_locked ?
			 CAPSULE_TPM_ANCHOR_WRITELOCKED : 0)) &&
		append_be16(&buffer, CAPSULE_TPM_ANCHOR_POLICY_SIZE) &&
		append(&buffer, policy, CAPSULE_TPM_ANCHOR_POLICY_SIZE) &&
		append_be16(&buffer, CAPSULE_TPM_ANCHOR_SIZE) &&
		buffer.size == sizeof(public_area);
	name[0] = TPM_ALG_SHA256 >> 8;
	name[1] = TPM_ALG_SHA256;
	valid &= sha256(public_area, sizeof(public_area), name + 2);
	secure_clear(public_area, sizeof(public_area));
	return valid;
}

static bool make_cp_hash(const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_value *candidate, bool write,
	const uint8_t auth_policy[CAPSULE_TPM_ANCHOR_POLICY_SIZE],
	uint8_t digest[CAPSULE_TPM_ANCHOR_POLICY_SIZE])
{
	uint8_t name[CAPSULE_TPM_ANCHOR_AUTHORITY_NAME_SIZE];
	uint8_t command[4];
	uint8_t parameters[44];
	struct buffer buffer = { .data = parameters, .capacity = sizeof(parameters) };
	const void *parts[] = { command, name, name, parameters };
	size_t sizes[] = { sizeof(command), sizeof(name), sizeof(name), 0 };
	uint32_t code = write ? TPM2_NV_Write : TPM2_NV_WriteLock;
	bool valid;

	command[0] = code >> 24;
	command[1] = code >> 16;
	command[2] = code >> 8;
	command[3] = code;
	valid = make_nv_name(binding, auth_policy, name);
	if (write)
		valid &= append_be16(&buffer, sizeof(*candidate)) &&
			append(&buffer, candidate, sizeof(*candidate)) &&
			append_be16(&buffer, 0);
	sizes[3] = buffer.size;
	valid &= hash_parts(parts, sizes, ARRAY_SIZE(parts), digest);
	secure_clear(name, sizeof(name));
	secure_clear(command, sizeof(command));
	secure_clear(parameters, sizeof(parameters));
	return valid;
}

static bool exact_public(const struct tlcl2_transport *transport,
	const struct capsule_tpm_anchor_binding *binding,
	const uint8_t policy[CAPSULE_TPM_ANCHOR_POLICY_SIZE], bool *write_locked)
{
	struct tlcl2_nv_public public;
	bool valid;

	if (!write_locked)
		return false;
	*write_locked = false;
	memset(&public, 0, sizeof(public));
	valid = tlcl2_read_public_on(transport,
		binding->nv_index & 0x00ffffffU, &public) ==
		TPM_SUCCESS && public.index == (binding->nv_index & 0x00ffffffU) &&
		public.name_alg == TPM_ALG_SHA256 &&
		(public.attributes & ~CAPSULE_TPM_ANCHOR_WRITELOCKED) ==
			(CAPSULE_TPM_ANCHOR_ATTRIBUTES |
			 CAPSULE_TPM_ANCHOR_WRITTEN) &&
		public.auth_policy_size == CAPSULE_TPM_ANCHOR_POLICY_SIZE &&
		!memcmp(public.auth_policy, policy, sizeof(public.auth_policy[0]) *
			CAPSULE_TPM_ANCHOR_POLICY_SIZE) &&
		public.data_size == CAPSULE_TPM_ANCHOR_SIZE;
	if (valid)
		*write_locked = !!(public.attributes &
			CAPSULE_TPM_ANCHOR_WRITELOCKED);
	secure_clear(&public, sizeof(public));
	return valid;
}

struct transport_context {
	capsule_tpm_anchor_transmit_fn *transmit;
	void *context;
	bool failed;
};

static tpm_result_t lifecycle_sendrecv(void *opaque, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size)
{
	struct transport_context *context = opaque;

	if (!context || !context->transmit)
		return TPM_IOERROR;
	if (context->transmit(context->context, request, request_size, response,
		response_size) != CB_SUCCESS) {
		context->failed = true;
		return TPM_IOERROR;
	}
	return TPM_SUCCESS;
}

static tpm_result_t run_external(
	const struct tlcl2_external_object *object, void *opaque)
{
	struct operation_context *context = opaque;
	struct tlcl2_verified_ticket ticket = { 0 };
	tpm_result_t result = TPM_IOERROR;
	uint32_t code = context->write ? TPM2_NV_Write : TPM2_NV_WriteLock;

	if (memcmp(object->name, context->input.authority_name,
		sizeof(context->input.authority_name)) ||
	    object->name_size != sizeof(context->input.authority_name) ||
	    tlcl2_verify_rsa_signature(object, context->authorization_hash,
		sizeof(context->authorization_hash), context->input.policy.signature,
		context->input.policy.signature_size, &ticket) != TPM_SUCCESS ||
	    tlcl2_policy_authorize(context->session,
		context->input.policy.approved_policy,
		sizeof(context->input.policy.approved_policy),
		context->input.policy_ref, context->input.policy_ref_size, object->name,
		object->name_size, &ticket) != TPM_SUCCESS ||
	    tlcl2_policy_command_code(context->session, code) != TPM_SUCCESS ||
	    tlcl2_policy_or(context->session, context->branches, 2,
		CAPSULE_TPM_ANCHOR_POLICY_SIZE) != TPM_SUCCESS)
		goto out;
	if (context->write)
		context->command_attempted = true;
	if (context->write)
		result = context->command_result = tlcl2_policy_nv_write(
			context->session,
			context->input.binding.nv_index & 0x00ffffffU,
			(const uint8_t *)&context->input.candidate,
			sizeof(context->input.candidate), 0);
	else {
		context->command_attempted = true;
		result = context->command_result = tlcl2_policy_nv_write_lock(
			context->session,
			context->input.binding.nv_index & 0x00ffffffU);
	}
out:
	secure_clear(&ticket, sizeof(ticket));
	return result;
}

static tpm_result_t run_session(const struct tlcl2_policy_session *session,
	void *opaque)
{
	struct operation_context *context = opaque;
	const struct tlcl2_policy_authorization password = {
		.handle = TPM_RS_PW,
	};
	tpm_result_t cleanup_result = TPM_IOERROR;
	tpm_result_t result = TPM_IOERROR;

	context->session = session;
	if (tlcl2_policy_nv_written(session, true) != TPM_SUCCESS ||
	    tlcl2_policy_nv(session,
		context->input.binding.nv_index & 0x00ffffffU, &password,
		(const uint8_t *)context->old_value, sizeof(*context->old_value),
		0, 0) != TPM_SUCCESS ||
	    tlcl2_policy_cp_hash(session, context->cp_hash,
		sizeof(context->cp_hash)) != TPM_SUCCESS)
		goto out;
	result = tlcl2_external_object_run_on(context->transport, &context->key,
		run_external, context, &cleanup_result);
	if (cleanup_result != TPM_SUCCESS)
		context->cleanup_failed = true;
out:
	context->session = NULL;
	return result;
}

static bool callbacks_match(
	const struct capsule_tpm_anchor_policy_provider *state,
	const struct capsule_tpm_anchor_policy_callbacks *snapshot)
{
	return !memcmp(&state->callbacks, snapshot, sizeof(*snapshot)) &&
		!memcmp(&state->sealed_callbacks, snapshot, sizeof(*snapshot));
}

static bool state_is(const struct capsule_tpm_anchor_policy_provider *state,
	uint32_t control)
{
	return __atomic_load_n(&state->control, __ATOMIC_ACQUIRE) == control;
}

static bool attempts_are(const struct capsule_tpm_anchor_policy_provider *state,
	uint32_t write_attempted, uint32_t lock_attempted)
{
	return __atomic_load_n(&state->write_attempted, __ATOMIC_ACQUIRE) ==
		write_attempted &&
		__atomic_load_n(&state->lock_attempted, __ATOMIC_ACQUIRE) ==
		lock_attempted;
}

static bool state_zero_except_control(
	const struct capsule_tpm_anchor_policy_provider *state)
{
	return bytes_zero(state, offsetof(
		struct capsule_tpm_anchor_policy_provider, control)) &&
		bytes_zero((const uint8_t *)state + offsetof(
			struct capsule_tpm_anchor_policy_provider, write_attempted),
			sizeof(*state) - offsetof(
				struct capsule_tpm_anchor_policy_provider,
				write_attempted));
}

static void fail(struct capsule_tpm_anchor_policy_provider *state)
{
	if (state)
		__atomic_store_n(&state->control, PROVIDER_FAILED, __ATOMIC_RELEASE);
}

static enum cb_err prepared(const void *opaque,
	const struct capsule_tpm_anchor_grant *grant)
{
	struct capsule_tpm_anchor_policy_provider *state = (void *)opaque;
	struct capsule_tpm_anchor_grant snapshot;
	struct capsule_tpm_anchor_policy_callbacks callback_snapshot;
	uint32_t expected = PROVIDER_READY;

	if (!state || !grant ||
	    ranges_overlap(state, sizeof(*state), grant, sizeof(*grant)) ||
	    state->revision !=
		CAPSULE_TPM_ANCHOR_POLICY_PROVIDER_REVISION ||
	    state->size != sizeof(*state) ||
	    !__atomic_compare_exchange_n(&state->control, &expected,
		PROVIDER_PREPARING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		fail(state);
		return CB_ERR;
	}
	callback_snapshot = state->sealed_callbacks;
	if (!callbacks_match(state, &callback_snapshot) ||
	    !attempts_are(state, 0, 0)) {
		fail(state);
		return CB_ERR;
	}
	snapshot = *grant;
	if (callback_snapshot.prepared(callback_snapshot.context,
		&snapshot) != CB_SUCCESS ||
	    memcmp(&snapshot, grant, sizeof(snapshot)) ||
	    state->revision != CAPSULE_TPM_ANCHOR_POLICY_PROVIDER_REVISION ||
	    state->size != sizeof(*state) ||
	    !state_is(state, PROVIDER_PREPARING) ||
	    !callbacks_match(state, &callback_snapshot) ||
	    !attempts_are(state, 0, 0)) {
		secure_clear(&snapshot, sizeof(snapshot));
		fail(state);
		return CB_ERR;
	}
	secure_clear(&snapshot, sizeof(snapshot));
	expected = PROVIDER_PREPARING;
	if (!__atomic_compare_exchange_n(&state->control, &expected,
		PROVIDER_RUNNING, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		fail(state);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static enum cb_err read_anchor(const void *opaque,
	capsule_tpm_anchor_transmit_fn *transmit, void *transmit_context,
	const struct capsule_tpm_anchor_binding *expected,
	struct capsule_tpm_anchor_binding *observed,
	struct capsule_tpm_anchor_value *value)
{
	struct capsule_tpm_anchor_policy_provider *state = (void *)opaque;
	struct transport_context transport_context = {
		.transmit = transmit,
		.context = transmit_context,
	};
	const struct tlcl2_transport transport = {
		.sendrecv = lifecycle_sendrecv,
		.context = &transport_context,
	};
	uint8_t policy[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	uint8_t branches[2 * CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	struct capsule_tpm_anchor_value result = { 0 };
	struct capsule_tpm_anchor_binding binding;
	struct capsule_tpm_anchor_binding expected_snapshot;
	struct capsule_tpm_anchor_policy_callbacks callback_snapshot;
	uint32_t control = PROVIDER_RUNNING;
	uint32_t write_attempted;
	uint32_t lock_attempted;
	bool write_locked;
	bool clear_observed;
	bool clear_value;
	bool valid = false;

	clear_observed = observed &&
		(!state || !ranges_overlap(observed, sizeof(*observed), state,
			sizeof(*state))) &&
		(!expected || !ranges_overlap(observed, sizeof(*observed), expected,
			sizeof(*expected)));
	clear_value = value &&
		(!state || !ranges_overlap(value, sizeof(*value), state,
			sizeof(*state))) &&
		(!expected || !ranges_overlap(value, sizeof(*value), expected,
			sizeof(*expected)));
	if (clear_observed)
		memset(observed, 0, sizeof(*observed));
	if (clear_value)
		memset(value, 0, sizeof(*value));
	if (!state)
		return CB_ERR;
	if (!expected || !observed || !value ||
	    ranges_overlap(observed, sizeof(*observed), value, sizeof(*value)) ||
	    ranges_overlap(observed, sizeof(*observed), expected,
		sizeof(*expected)) ||
	    ranges_overlap(value, sizeof(*value), expected, sizeof(*expected)) ||
	    ranges_overlap(observed, sizeof(*observed), state, sizeof(*state)) ||
	    ranges_overlap(value, sizeof(*value), state, sizeof(*state)) ||
	    ranges_overlap(expected, sizeof(*expected), state, sizeof(*state))) {
		fail(state);
		return CB_ERR;
	}
	if (state->revision != CAPSULE_TPM_ANCHOR_POLICY_PROVIDER_REVISION ||
	    state->size != sizeof(*state) || !binding_valid(expected) ||
	    !transmit || !__atomic_compare_exchange_n(&state->control, &control,
		PROVIDER_READING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto out;
	write_attempted = __atomic_load_n(&state->write_attempted,
		__ATOMIC_ACQUIRE);
	lock_attempted = __atomic_load_n(&state->lock_attempted,
		__ATOMIC_ACQUIRE);
	callback_snapshot = state->sealed_callbacks;
	if (!callbacks_match(state, &callback_snapshot) ||
	    !make_static_policy(expected, policy, branches))
		goto out;
	expected_snapshot = *expected;
	binding = expected_snapshot;
	if (!exact_public(&transport, &binding, policy, &write_locked))
		goto out;
	binding.write_locked = write_locked;
	if (tlcl2_read_auth_on(&transport,
		binding.nv_index & 0x00ffffffU, &result,
		sizeof(result)) != TPM_SUCCESS)
		goto out;
	valid = state->revision == CAPSULE_TPM_ANCHOR_POLICY_PROVIDER_REVISION &&
		state->size == sizeof(*state) && !transport_context.failed &&
		callbacks_match(state, &callback_snapshot) &&
		attempts_are(state, write_attempted, lock_attempted) &&
		!memcmp(expected, &expected_snapshot, sizeof(expected_snapshot));
	control = PROVIDER_READING;
	valid &= __atomic_compare_exchange_n(&state->control, &control,
		PROVIDER_RUNNING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	if (valid) {
		*observed = binding;
		*value = result;
	}
out:
	secure_clear(policy, sizeof(policy));
	secure_clear(branches, sizeof(branches));
	secure_clear(&result, sizeof(result));
	secure_clear(&expected_snapshot, sizeof(expected_snapshot));
	if (!valid) {
		fail(state);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static enum cb_err operate(const void *opaque,
	capsule_tpm_anchor_transmit_fn *transmit, void *transmit_context,
	const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_authorization *authorization, bool write)
{
	struct capsule_tpm_anchor_policy_provider *state = (void *)opaque;
	struct transport_context transport_context = {
		.transmit = transmit,
		.context = transmit_context,
	};
	const struct tlcl2_transport transport = {
		.sendrecv = lifecycle_sendrecv,
		.context = &transport_context,
	};
	struct operation_context context = { 0 };
	uint8_t auth_policy[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	uint8_t authorization_check[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
	const void *hash_parts_data[2];
	size_t hash_parts_sizes[2];
	uint32_t expected = 0;
	uint32_t control = PROVIDER_RUNNING;
	uint32_t *attempted;
	uint32_t other_attempted;
	tpm_result_t session_result = TPM_IOERROR;
	tpm_result_t session_cleanup = TPM_IOERROR;
	struct capsule_tpm_anchor_policy_callbacks callback_snapshot;
	bool invariant_valid;
	bool advisory = false;
	bool valid = false;
	bool restored = false;
	bool write_locked;

	if (!state)
		goto out;
	if (!transmit ||
	    !authorization_valid(authorization, binding) ||
	    binding->write_locked ||
	    ranges_overlap(binding, sizeof(*binding), authorization,
		sizeof(*authorization)) ||
	    ranges_overlap(binding, sizeof(*binding), state, sizeof(*state)) ||
	    ranges_overlap(authorization, sizeof(*authorization), state,
		sizeof(*state))) {
		fail(state);
		goto out;
	}
	callback_snapshot = state->sealed_callbacks;
	if (state->revision != CAPSULE_TPM_ANCHOR_POLICY_PROVIDER_REVISION ||
	    state->size != sizeof(*state) ||
	    !callbacks_match(state, &callback_snapshot)) {
		fail(state);
		goto out;
	}
	if (!__atomic_compare_exchange_n(&state->control, &control,
		PROVIDER_OPERATING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		fail(state);
		goto out;
	}
	if (!sha256(authorization, sizeof(*authorization), context.input_hash)) {
		fail(state);
		goto out;
	}
	context.input.binding = *binding;
	context.input.current = authorization->current;
	context.input.candidate = authorization->candidate;
	memcpy(context.input.authority_name, authorization->authority_name,
		sizeof(context.input.authority_name));
	memcpy(context.input.policy_ref, authorization->policy_ref,
		sizeof(context.input.policy_ref));
	context.input.policy_ref_size = authorization->policy_ref_size;
	memcpy(context.key.modulus, authorization->modulus,
		sizeof(context.key.modulus));
	context.key.modulus_size = authorization->modulus_size;
	context.input.policy = write ? authorization->write : authorization->lock;
	context.transport = &transport;
	context.old_value = write ? &context.input.current :
		&context.input.candidate;
	context.write = write;
	hash_parts_data[0] = context.input.policy.approved_policy;
	hash_parts_data[1] = context.input.policy_ref;
	hash_parts_sizes[0] = CAPSULE_TPM_ANCHOR_POLICY_SIZE;
	hash_parts_sizes[1] = context.input.policy_ref_size;
	attempted = write ? &state->write_attempted : &state->lock_attempted;
	other_attempted = __atomic_load_n(write ? &state->lock_attempted :
		&state->write_attempted, __ATOMIC_ACQUIRE);
	if (!__atomic_compare_exchange_n(attempted, &expected, 1, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		fail(state);
		goto out;
	}
	if (!sha256(authorization, sizeof(*authorization), authorization_check) ||
	    memcmp(authorization_check, context.input_hash,
		sizeof(authorization_check)) ||
	    memcmp(binding, &context.input.binding, sizeof(*binding)) ||
	    !make_static_policy(&context.input.binding, auth_policy,
		context.branches) ||
	    !make_cp_hash(&context.input.binding, &context.input.candidate, write,
		auth_policy, context.cp_hash) ||
	    memcmp(context.cp_hash, context.input.policy.cp_hash,
		sizeof(context.cp_hash)) ||
	    !hash_parts(hash_parts_data, hash_parts_sizes,
		ARRAY_SIZE(hash_parts_data), context.authorization_hash)) {
		fail(state);
		goto out;
	}
	if (exact_public(&transport, &context.input.binding, auth_policy,
		&write_locked) &&
	    !write_locked)
		session_result = tlcl2_policy_session_run_on(&transport,
			TPM_ALG_SHA256, context.input.policy.nonce,
			sizeof(context.input.policy.nonce), run_session, &context,
			&session_cleanup);
	valid = session_result == TPM_SUCCESS && !context.cleanup_failed &&
		session_cleanup == TPM_SUCCESS;
	advisory = context.command_attempted && !context.cleanup_failed &&
		session_cleanup == TPM_SUCCESS &&
		session_result == context.command_result &&
		context.command_result == TPM_IOERROR;
	invariant_valid = state->revision ==
		CAPSULE_TPM_ANCHOR_POLICY_PROVIDER_REVISION &&
		state->size == sizeof(*state) &&
		callbacks_match(state, &callback_snapshot) &&
		__atomic_load_n(attempted, __ATOMIC_ACQUIRE) == 1 &&
		__atomic_load_n(write ? &state->lock_attempted :
			&state->write_attempted, __ATOMIC_ACQUIRE) == other_attempted &&
		!transport_context.failed &&
		!memcmp(binding, &context.input.binding, sizeof(*binding));
	if (invariant_valid) {
		invariant_valid = sha256(authorization, sizeof(*authorization),
			authorization_check) &&
			!memcmp(authorization_check, context.input_hash,
				sizeof(authorization_check));
	}
	advisory &= !transport_context.failed;
	valid &= invariant_valid;
	control = PROVIDER_OPERATING;
	if ((valid || advisory) && invariant_valid)
		restored = __atomic_compare_exchange_n(&state->control, &control,
			PROVIDER_RUNNING, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE);
	if (!restored) {
		fail(state);
		valid = false;
	}
out:
	secure_clear(auth_policy, sizeof(auth_policy));
	secure_clear(authorization_check, sizeof(authorization_check));
	secure_clear(&context, sizeof(context));
	/* A TPM result is advisory; the coordinator performs exact readback. */
	return valid ? CB_SUCCESS : CB_ERR;
}

static enum cb_err write_anchor(const void *opaque,
	capsule_tpm_anchor_transmit_fn *transmit, void *transmit_context,
	const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_authorization *authorization)
{
	return operate(opaque, transmit, transmit_context, binding,
		authorization, true);
}

static enum cb_err lock_anchor(const void *opaque,
	capsule_tpm_anchor_transmit_fn *transmit, void *transmit_context,
	const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_authorization *authorization)
{
	return operate(opaque, transmit, transmit_context, binding,
		authorization, false);
}

static enum cb_err install(const void *opaque,
	const struct capsule_tpm_anchor_grant *grant,
	const struct capsule_tpm_anchor_binding *binding)
{
	struct capsule_tpm_anchor_policy_provider *state = (void *)opaque;
	struct capsule_tpm_anchor_grant grant_snapshot;
	struct capsule_tpm_anchor_binding binding_snapshot;
	struct capsule_tpm_anchor_policy_callbacks callback_snapshot;
	uint32_t expected = PROVIDER_RUNNING;
	uint32_t write_attempted;
	uint32_t lock_attempted;
	enum cb_err result;

	if (!state || !grant || !binding_valid(binding) ||
	    state->revision != CAPSULE_TPM_ANCHOR_POLICY_PROVIDER_REVISION ||
	    state->size != sizeof(*state) ||
	    !binding->write_locked ||
	    ranges_overlap(grant, sizeof(*grant), binding, sizeof(*binding)) ||
	    ranges_overlap(grant, sizeof(*grant), state, sizeof(*state)) ||
	    ranges_overlap(binding, sizeof(*binding), state, sizeof(*state)) ||
	    capsule_tpm_anchor_grant_validate(grant, binding) != CB_SUCCESS ||
	    !__atomic_compare_exchange_n(&state->control, &expected,
		PROVIDER_INSTALLING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		fail(state);
		return CB_ERR;
	}
	callback_snapshot = state->sealed_callbacks;
	write_attempted = __atomic_load_n(&state->write_attempted,
		__ATOMIC_ACQUIRE);
	lock_attempted = __atomic_load_n(&state->lock_attempted,
		__ATOMIC_ACQUIRE);
	if (!callbacks_match(state, &callback_snapshot)) {
		fail(state);
		return CB_ERR;
	}
	grant_snapshot = *grant;
	binding_snapshot = *binding;
	result = callback_snapshot.install(callback_snapshot.context,
		&grant_snapshot, &binding_snapshot);
	if (result != CB_SUCCESS ||
	    memcmp(&grant_snapshot, grant, sizeof(grant_snapshot)) ||
	    memcmp(&binding_snapshot, binding, sizeof(binding_snapshot)) ||
	    state->revision != CAPSULE_TPM_ANCHOR_POLICY_PROVIDER_REVISION ||
	    state->size != sizeof(*state) ||
	    !state_is(state, PROVIDER_INSTALLING) ||
	    !callbacks_match(state, &callback_snapshot) ||
	    !attempts_are(state, write_attempted, lock_attempted)) {
		secure_clear(&grant_snapshot, sizeof(grant_snapshot));
		secure_clear(&binding_snapshot, sizeof(binding_snapshot));
		fail(state);
		return CB_ERR;
	}
	secure_clear(&grant_snapshot, sizeof(grant_snapshot));
	secure_clear(&binding_snapshot, sizeof(binding_snapshot));
	expected = PROVIDER_INSTALLING;
	if (!__atomic_compare_exchange_n(&state->control, &expected,
		PROVIDER_FINISHED, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		fail(state);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum cb_err capsule_tpm_anchor_policy_provider_init(
	struct capsule_tpm_anchor_policy_provider *state,
	const struct capsule_tpm_anchor_policy_callbacks *callbacks,
	struct capsule_tpm_anchor_transition_provider *provider)
{
	struct capsule_tpm_anchor_policy_callbacks callback_snapshot;
	struct capsule_tpm_anchor_transition_provider output = {
		.revision = CAPSULE_TPM_ANCHOR_TRANSITION_PROVIDER_REVISION,
		.size = sizeof(output),
		.prepared = prepared,
		.read = read_anchor,
		.write = write_anchor,
		.lock = lock_anchor,
		.install = install,
		.context = state,
		.context_size = sizeof(*state),
	};
	uint32_t expected = 0;

	if (!state || !callbacks || !provider)
		return CB_ERR_ARG;
	if (ranges_overlap(state, sizeof(*state), callbacks,
		sizeof(*callbacks)) ||
	    ranges_overlap(state, sizeof(*state), provider,
		sizeof(*provider)) ||
		ranges_overlap(callbacks, sizeof(*callbacks), provider,
			sizeof(*provider)))
		return CB_ERR_ARG;
	callback_snapshot = *callbacks;
	if (callback_snapshot.context && callback_snapshot.context_size &&
	    (ranges_overlap(state, sizeof(*state), callback_snapshot.context,
		callback_snapshot.context_size) ||
	     ranges_overlap(callbacks, sizeof(*callbacks),
		callback_snapshot.context, callback_snapshot.context_size) ||
	     ranges_overlap(provider, sizeof(*provider),
		callback_snapshot.context, callback_snapshot.context_size)))
		return CB_ERR_ARG;
	if (!__atomic_compare_exchange_n(&state->control, &expected,
		PROVIDER_INITIALIZING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE)) {
		fail(state);
		return CB_ERR;
	}
	if (!state_zero_except_control(state) || !callback_snapshot.prepared ||
	    !callback_snapshot.install ||
	    (!!callback_snapshot.context != !!callback_snapshot.context_size)) {
		fail(state);
		return CB_ERR_ARG;
	}
	state->revision = CAPSULE_TPM_ANCHOR_POLICY_PROVIDER_REVISION;
	state->size = sizeof(*state);
	state->write_attempted = 0;
	state->lock_attempted = 0;
	state->callbacks = callback_snapshot;
	state->sealed_callbacks = callback_snapshot;
	expected = PROVIDER_INITIALIZING;
	if (!__atomic_compare_exchange_n(&state->control, &expected,
		PROVIDER_READY, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		fail(state);
		return CB_ERR;
	}
	*provider = output;
	return CB_SUCCESS;
}
