/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/capsule_anchor_transition.h>
#include <security/tpm/tss/tcg-2.0/tss_structures.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

enum operation {
	OP_READ = 1,
	OP_ADVANCE = 2,
};

struct backend_state {
	struct capsule_tpm_anchor_value value;
	struct capsule_tpm_anchor_value candidate;
	unsigned int begin_count;
	unsigned int transmit_count;
	unsigned int release_count;
};

struct backend_context {
	struct backend_state *state;
};

struct provider_context {
	struct capsule_tpm_anchor_authorization *original;
	unsigned int prepared_count;
	unsigned int read_count;
	unsigned int advance_count;
	unsigned int install_count;
	unsigned int mutate_at;
	bool prepared_fails;
	bool advance_fails;
	bool advance_reports_failure;
	bool corrupt_readback;
	bool starts_at_candidate;
	unsigned int grant_mutation;
	bool restore_grant;
	const struct capsule_tpm_anchor_grant *retained_grant;
};

static enum cb_err backend_begin(void *opaque)
{
	struct backend_context *context = opaque;

	context->state->begin_count++;
	return CB_SUCCESS;
}

static enum cb_err backend_transmit(void *opaque, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size)
{
	struct backend_context *context = opaque;
	struct backend_state *state = context->state;

	state->transmit_count++;
	if (request_size != 1 || !response ||
	    *response_size < sizeof(state->value))
		return CB_ERR;
	if (request[0] == OP_ADVANCE)
		state->value = state->candidate;
	else if (request[0] != OP_READ)
		return CB_ERR;
	memcpy(response, &state->value, sizeof(state->value));
	*response_size = sizeof(state->value);
	return CB_SUCCESS;
}

static enum cb_err backend_quiesce(void *opaque)
{
	(void)opaque;
	return CB_SUCCESS;
}

static enum cb_err backend_release(void *opaque)
{
	struct backend_context *context = opaque;

	context->state->release_count++;
	return CB_SUCCESS;
}

static enum cb_err prepared(const void *opaque,
	const struct capsule_tpm_anchor_grant *grant)
{
	struct provider_context *context = (void *)opaque;
	struct capsule_tpm_anchor_grant original = *grant;
	struct capsule_tpm_anchor_grant *mutable_grant = (void *)grant;

	context->prepared_count++;
	CHECK(grant->flags == 0);
	context->retained_grant = grant;
	switch (context->grant_mutation) {
	case 1:
		mutable_grant->generation++;
		break;
	case 2:
		mutable_grant->transaction++;
		break;
	case 3:
		mutable_grant->current.epoch += 2;
		mutable_grant->candidate.epoch += 2;
		mutable_grant->current.digest[0] ^= 0x5a;
		mutable_grant->candidate.digest[0] ^= 0xa5;
		break;
	case 4:
		mutable_grant->current.digest[0] ^= 1;
		break;
	case 5:
		mutable_grant->candidate.digest[0] ^= 1;
		break;
	case 6:
		mutable_grant->current.epoch++;
		mutable_grant->candidate.epoch++;
		break;
	}
	if (context->restore_grant)
		*mutable_grant = original;
	if (context->mutate_at == 1)
		context->original->transaction++;
	return context->prepared_fails ? CB_ERR : CB_SUCCESS;
}

static enum cb_err read_anchor(const void *opaque,
	capsule_tpm_anchor_transmit_fn *transmit, void *transmit_context,
	const struct capsule_tpm_anchor_binding *binding,
	struct capsule_tpm_anchor_value *value)
{
	struct provider_context *context = (void *)opaque;
	uint8_t request = OP_READ;
	size_t size = sizeof(*value);

	(void)binding;
	context->read_count++;
	if (transmit(transmit_context, &request, sizeof(request), (void *)value,
		&size) != CB_SUCCESS || size != sizeof(*value))
		return CB_ERR;
	if (context->corrupt_readback && context->read_count == 2)
		value->digest[0] ^= 1;
	return CB_SUCCESS;
}

static enum cb_err advance(const void *opaque,
	capsule_tpm_anchor_transmit_fn *transmit, void *transmit_context,
	const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_authorization *authorization)
{
	struct provider_context *context = (void *)opaque;
	struct capsule_tpm_anchor_value ignored;
	uint8_t request = OP_ADVANCE;
	size_t size = sizeof(ignored);

	(void)binding;
	CHECK(authorization->cp_hash[0] == 0x66);
	context->advance_count++;
	if (context->restore_grant) {
		struct capsule_tpm_anchor_grant *retained =
			(void *)context->retained_grant;
		struct capsule_tpm_anchor_grant saved = *retained;

		retained->generation++;
		*retained = saved;
	}
	if (context->mutate_at == 2)
		context->original->cp_hash[0] ^= 1;
	if (context->advance_fails)
		return CB_ERR;
	if (transmit(transmit_context, &request, sizeof(request),
		(void *)&ignored, &size) != CB_SUCCESS)
		return CB_ERR;
	return context->advance_reports_failure ? CB_ERR : CB_SUCCESS;
}

static enum cb_err install(const void *opaque,
	const struct capsule_tpm_anchor_grant *grant,
	const struct capsule_tpm_anchor_binding *binding)
{
	struct provider_context *context = (void *)opaque;

	context->install_count++;
	CHECK(grant->flags == CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS);
	CHECK(grant->nv_index == binding->nv_index);
	CHECK(grant->generation == context->original->generation);
	CHECK(grant->transaction == context->original->transaction);
	CHECK(!memcmp(&grant->current, &context->original->current,
		sizeof(grant->current)));
	CHECK(!memcmp(&grant->candidate, &context->original->candidate,
		sizeof(grant->candidate)));
	if (context->mutate_at == 3)
		context->original->signature[0] ^= 1;
	return CB_SUCCESS;
}

static struct capsule_tpm_anchor_binding valid_binding(void)
{
	struct capsule_tpm_anchor_binding binding = {
		.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION,
		.nv_index = HR_NV_INDEX | 0x150001,
		.policy_ref_size = 7,
		.write_locked = 1,
	};

	binding.authority_name[0] = TPM_ALG_SHA256 >> 8;
	binding.authority_name[1] = TPM_ALG_SHA256;
	memset(binding.authority_name + 2, 0x44,
		sizeof(binding.authority_name) - 2);
	memcpy(binding.policy_ref, "capsule", binding.policy_ref_size);
	return binding;
}

static struct capsule_tpm_anchor_authorization valid_authorization(
	const struct capsule_tpm_anchor_binding *binding)
{
	struct capsule_tpm_anchor_authorization authorization = {
		.revision = CAPSULE_TPM_ANCHOR_AUTHORIZATION_REVISION,
		.size = sizeof(authorization),
		.policy_revision = binding->policy_revision,
		.nv_index = binding->nv_index,
		.generation = 9,
		.transaction = 17,
		.current.epoch = 3,
		.candidate.epoch = 4,
		.policy_ref_size = binding->policy_ref_size,
		.modulus_size = 256,
		.signature_size = 256,
	};

	memset(authorization.current.digest, 0x11,
		sizeof(authorization.current.digest));
	memset(authorization.candidate.digest, 0x22,
		sizeof(authorization.candidate.digest));
	memset(authorization.approved_policy, 0x55,
		sizeof(authorization.approved_policy));
	memset(authorization.cp_hash, 0x66, sizeof(authorization.cp_hash));
	memcpy(authorization.authority_name, binding->authority_name,
		sizeof(authorization.authority_name));
	memcpy(authorization.policy_ref, binding->policy_ref,
		sizeof(authorization.policy_ref));
	memset(authorization.nonce, 0x77, sizeof(authorization.nonce));
	memset(authorization.modulus, 0x88, authorization.modulus_size);
	memset(authorization.signature, 0x99, authorization.signature_size);
	return authorization;
}

static enum cb_err run_case(struct provider_context *provider_context,
	struct capsule_tpm_anchor_authorization *authorization,
	const struct capsule_tpm_anchor_binding *binding,
	struct capsule_tpm_anchor_transition *transition,
	struct tpm_pre_os_lifecycle *lifecycle)
{
	struct backend_state backend_state = {
		.value = provider_context->starts_at_candidate ?
			authorization->candidate : authorization->current,
		.candidate = authorization->candidate,
	};
	struct backend_context backend_context = { .state = &backend_state };
	const struct tpm_pre_os_backend backend = {
		.begin = backend_begin,
		.transmit = backend_transmit,
		.quiesce = backend_quiesce,
		.release = backend_release,
	};
	const struct capsule_tpm_anchor_transition_provider provider = {
		.revision = CAPSULE_TPM_ANCHOR_TRANSITION_PROVIDER_REVISION,
		.size = sizeof(provider),
		.prepared = prepared,
		.read = read_anchor,
		.advance = advance,
		.install = install,
		.context = provider_context,
		.context_size = sizeof(*provider_context),
	};

	CHECK(tpm_pre_os_lifecycle_install(lifecycle, &backend,
		&backend_context, sizeof(backend_context)) == CB_SUCCESS);
	return capsule_tpm_anchor_transition_run(transition, lifecycle, binding,
		authorization, &provider);
}

static void test_reset_recovery_and_ambiguous_advance(void)
{
	for (unsigned int recovery = 0; recovery < 2; recovery++) {
		struct capsule_tpm_anchor_binding binding = valid_binding();
		struct capsule_tpm_anchor_authorization authorization =
			valid_authorization(&binding);
		struct provider_context context = {
			.original = &authorization,
			.starts_at_candidate = recovery == 0,
			.advance_reports_failure = recovery == 1,
		};
		struct capsule_tpm_anchor_transition transition = { 0 };
		struct tpm_pre_os_lifecycle lifecycle = { 0 };

		CHECK(run_case(&context, &authorization, &binding, &transition,
			&lifecycle) == CB_SUCCESS);
		CHECK(context.install_count == 1);
		CHECK(context.advance_count == (recovery == 1));
	}
}

static void test_malformed_authorization(void)
{
	for (unsigned int field = 0; field < 15; field++) {
		struct capsule_tpm_anchor_binding binding = valid_binding();
		struct capsule_tpm_anchor_authorization authorization =
			valid_authorization(&binding);
		struct provider_context context = { .original = &authorization };
		struct capsule_tpm_anchor_transition transition = { 0 };
		struct tpm_pre_os_lifecycle lifecycle = { 0 };

		switch (field) {
		case 0:
			authorization.revision++;
			break;
		case 1:
			authorization.size--;
			break;
		case 2:
			authorization.nv_index++;
			break;
		case 3:
			authorization.current.epoch = 0;
			break;
		case 4:
			authorization.candidate.epoch++;
			break;
		case 5:
			memset(authorization.approved_policy, 0,
				sizeof(authorization.approved_policy));
			break;
		case 6:
			memset(authorization.cp_hash, 0,
				sizeof(authorization.cp_hash));
			break;
		case 7:
			authorization.authority_name[2] ^= 1;
			break;
		case 8:
			authorization.policy_ref_size++;
			break;
		case 9:
			authorization.modulus_size = 255;
			break;
		case 10:
			authorization.signature_size--;
			break;
		case 11:
			authorization.reserved = 1;
			break;
		case 12:
			memset(authorization.nonce, 0,
				sizeof(authorization.nonce));
			break;
		case 13:
			authorization.modulus[300] = 1;
			break;
		case 14:
			authorization.reserved2 = 1;
			break;
		}
		CHECK(run_case(&context, &authorization, &binding, &transition,
			&lifecycle) == CB_ERR);
		CHECK(!context.prepared_count && !context.read_count &&
			!context.advance_count && !context.install_count);
		CHECK(tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
	}
}

static void test_prepared_argument_is_immutable(void)
{
	for (unsigned int mutation = 1; mutation <= 6; mutation++) {
		struct capsule_tpm_anchor_binding binding = valid_binding();
		struct capsule_tpm_anchor_authorization authorization =
			valid_authorization(&binding);
		struct provider_context context = {
			.original = &authorization,
			.grant_mutation = mutation,
		};
		struct capsule_tpm_anchor_transition transition = { 0 };
		struct tpm_pre_os_lifecycle lifecycle = { 0 };

		CHECK(run_case(&context, &authorization, &binding, &transition,
			&lifecycle) == CB_ERR);
		CHECK(context.prepared_count == 1 && !context.read_count &&
			!context.advance_count && !context.install_count);
	}
}

static void test_restored_prepared_argument_cannot_substitute_grant(void)
{
	struct capsule_tpm_anchor_binding binding = valid_binding();
	struct capsule_tpm_anchor_authorization authorization =
		valid_authorization(&binding);
	struct provider_context context = {
		.original = &authorization,
		.grant_mutation = 3,
		.restore_grant = true,
	};
	struct capsule_tpm_anchor_transition transition = { 0 };
	struct tpm_pre_os_lifecycle lifecycle = { 0 };

	CHECK(run_case(&context, &authorization, &binding, &transition,
		&lifecycle) == CB_SUCCESS);
	CHECK(context.install_count == 1);
}

static void test_success_and_replay(void)
{
	struct capsule_tpm_anchor_binding binding = valid_binding();
	struct capsule_tpm_anchor_authorization authorization =
		valid_authorization(&binding);
	struct provider_context context = { .original = &authorization };
	struct capsule_tpm_anchor_transition transition = { 0 };
	struct tpm_pre_os_lifecycle lifecycle = { 0 };

	CHECK(run_case(&context, &authorization, &binding, &transition,
		&lifecycle) == CB_SUCCESS);
	CHECK(context.prepared_count == 1 && context.read_count == 2 &&
		context.advance_count == 1 && context.install_count == 1);
	CHECK(tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
	CHECK(capsule_tpm_anchor_transition_run(&transition, &lifecycle, &binding,
		&authorization, NULL) == CB_ERR);
}

static void test_prepared_is_mandatory(void)
{
	struct capsule_tpm_anchor_binding binding = valid_binding();
	struct capsule_tpm_anchor_authorization authorization =
		valid_authorization(&binding);
	struct provider_context context = {
		.original = &authorization,
		.prepared_fails = true,
	};
	struct capsule_tpm_anchor_transition transition = { 0 };
	struct tpm_pre_os_lifecycle lifecycle = { 0 };

	CHECK(run_case(&context, &authorization, &binding, &transition,
		&lifecycle) == CB_ERR);
	CHECK(context.prepared_count == 1 && !context.read_count &&
		!context.advance_count && !context.install_count);
}

static void test_mutation_fails_closed(void)
{
	for (unsigned int point = 1; point <= 2; point++) {
		struct capsule_tpm_anchor_binding binding = valid_binding();
		struct capsule_tpm_anchor_authorization authorization =
			valid_authorization(&binding);
		struct provider_context context = {
			.original = &authorization,
			.mutate_at = point,
		};
		struct capsule_tpm_anchor_transition transition = { 0 };
		struct tpm_pre_os_lifecycle lifecycle = { 0 };

		CHECK(run_case(&context, &authorization, &binding, &transition,
			&lifecycle) == CB_ERR);
		CHECK(!context.install_count);
	}
}

static void test_readback_and_advance_fail_closed(void)
{
	for (unsigned int mode = 0; mode < 2; mode++) {
		struct capsule_tpm_anchor_binding binding = valid_binding();
		struct capsule_tpm_anchor_authorization authorization =
			valid_authorization(&binding);
		struct provider_context context = {
			.original = &authorization,
			.advance_fails = mode == 0,
			.corrupt_readback = mode == 1,
		};
		struct capsule_tpm_anchor_transition transition = { 0 };
		struct tpm_pre_os_lifecycle lifecycle = { 0 };

		CHECK(run_case(&context, &authorization, &binding, &transition,
			&lifecycle) == CB_ERR);
		CHECK(!context.install_count);
		CHECK(tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
	}
}

int main(void)
{
	test_success_and_replay();
	test_prepared_is_mandatory();
	test_mutation_fails_closed();
	test_readback_and_advance_fail_closed();
	test_reset_recovery_and_ambiguous_advance();
	test_malformed_authorization();
	test_prepared_argument_is_immutable();
	test_restored_prepared_argument_cannot_substitute_grant();
	return 0;
}
