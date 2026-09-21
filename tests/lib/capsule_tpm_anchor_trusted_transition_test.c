/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/capsule_anchor_transition.h>
#include <security/tpm/tss/tcg-2.0/tss_structures.h>
#include <stdlib.h>
#include <string.h>

extern int fflush(void *stream);

#define CHECK(condition) do { if (!(condition)) { \
	__builtin_printf("%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	fflush(NULL); \
	abort(); \
} } while (0)

static const uint8_t command_success[] = {
	0x80, 0x02, 0, 0, 0, 0x13, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const uint8_t property_success[] = {
	0x80, 0x01, 0, 0, 0, 0x1b, 0, 0, 0, 0,
	0, 0, 0, 0, 0x06, 0, 0, 0, 1, 0, 0x00, 0x02, 0x01,
	0, 0, 0, 0x08,
};
static const uint8_t delivered_error[] = {
	0x80, 0x01, 0, 0, 0, 0x0a, 0, 0, 1, 1,
};

enum initial_state {
	CURRENT_UNLOCKED,
	CURRENT_LOCKED,
	CANDIDATE_UNLOCKED,
	CANDIDATE_LOCKED,
	THIRD_UNLOCKED,
};

struct backend_context {
	struct capsule_tpm_anchor_value value;
	struct capsule_tpm_anchor_value candidate;
	bool locked;
	bool ph_enable;
	bool advisory_write;
	bool advisory_lock;
	bool advisory_close;
	bool write_locks;
	unsigned int malformed_at;
	unsigned int transport_at;
	unsigned int command_count;
	unsigned int write_count;
	unsigned int lock_count;
	unsigned int close_count;
	unsigned int verify_count;
};

struct backend_ref {
	struct backend_context *context;
};

struct provider_context {
	struct backend_context *backend;
	struct capsule_tpm_anchor_platform_request *original;
	struct capsule_tpm_anchor_transition *transition;
	struct tpm_pre_os_lifecycle *lifecycle;
	struct capsule_tpm_anchor_binding *binding;
	const struct capsule_tpm_anchor_trusted_transition_provider *provider;
	unsigned int prepared_count;
	unsigned int read_count;
	unsigned int install_count;
	unsigned int mutate_at;
	bool prepared_fails;
	bool install_fails;
	bool reenter;
};

static enum cb_err begin(void *context)
{
	(void)context;
	return CB_SUCCESS;
}

static enum cb_err transmit(void *opaque, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size)
{
	struct backend_context *context = ((struct backend_ref *)opaque)->context;
	const uint8_t *reply = command_success;
	size_t reply_size = sizeof(command_success);
	uint32_t command;

	if (request_size == 1) {
		CHECK(*response_size >= sizeof(context->value));
		memcpy(response, &context->value, sizeof(context->value));
		*response_size = sizeof(context->value);
		return CB_SUCCESS;
	}
	CHECK(request_size >= 10);
	command = ((uint32_t)request[6] << 24) | ((uint32_t)request[7] << 16) |
		((uint32_t)request[8] << 8) | request[9];
	context->command_count++;
	if (context->transport_at == context->command_count)
		return CB_ERR;
	if (command == 0x137) {
		context->write_count++;
		context->value = context->candidate;
		context->locked = context->write_locks;
		if (context->advisory_write) {
			reply = delivered_error;
			reply_size = sizeof(delivered_error);
		}
	} else if (command == 0x138) {
		context->lock_count++;
		context->locked = true;
		if (context->advisory_lock) {
			reply = delivered_error;
			reply_size = sizeof(delivered_error);
		}
	} else if (command == 0x121) {
		context->close_count++;
		context->ph_enable = false;
		if (context->advisory_close) {
			reply = delivered_error;
			reply_size = sizeof(delivered_error);
		}
	} else if (command == 0x17a) {
		context->verify_count++;
		reply = property_success;
		reply_size = sizeof(property_success);
	} else {
		CHECK(false);
	}
	CHECK(*response_size >= reply_size);
	memcpy(response, reply, reply_size);
	if (context->malformed_at == context->command_count)
		response[5]--;
	*response_size = reply_size;
	return CB_SUCCESS;
}

static enum cb_err quiesce(void *context)
{
	(void)context;
	return CB_SUCCESS;
}

static enum cb_err release(void *context)
{
	(void)context;
	return CB_SUCCESS;
}

static enum cb_err prepared(const void *opaque,
	const struct capsule_tpm_anchor_grant *grant)
{
	struct provider_context *context = (void *)opaque;

	context->prepared_count++;
	CHECK(grant->flags == 0);
	if (context->reenter)
		CHECK(capsule_tpm_anchor_transition_run_trusted(context->transition,
			context->lifecycle, context->binding, context->original,
			context->provider) == CB_ERR);
	if (context->mutate_at == 1)
		context->original->generation++;
	return context->prepared_fails ? CB_ERR : CB_SUCCESS;
}

static enum cb_err read_anchor(const void *opaque,
	capsule_tpm_anchor_transmit_fn *send, void *send_context,
	const struct capsule_tpm_anchor_binding *expected,
	struct capsule_tpm_anchor_binding *observed,
	struct capsule_tpm_anchor_value *value)
{
	struct provider_context *context = (void *)opaque;
	uint8_t request = 0;
	size_t size = sizeof(*value);

	context->read_count++;
	*observed = *expected;
	if (send(send_context, &request, sizeof(request), (uint8_t *)value,
		&size) != CB_SUCCESS)
		return CB_ERR;
	CHECK(size == sizeof(*value));
	observed->write_locked = context->backend->locked;
	if (context->mutate_at == 2)
		context->original->transaction++;
	return CB_SUCCESS;
}

static enum cb_err install(const void *opaque,
	const struct capsule_tpm_anchor_grant *grant,
	const struct capsule_tpm_anchor_binding *binding)
{
	struct provider_context *context = (void *)opaque;

	context->install_count++;
	CHECK(grant->flags == CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS);
	CHECK(binding->write_locked == 1);
	if (context->mutate_at == 3)
		context->original->candidate.digest[0] ^= 1;
	return context->install_fails ? CB_ERR : CB_SUCCESS;
}

static struct capsule_tpm_anchor_binding valid_binding(void)
{
	return (struct capsule_tpm_anchor_binding) {
		.policy_revision = CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION,
		.nv_index = HR_NV_INDEX | 0x20,
	};
}

static struct capsule_tpm_anchor_platform_request valid_request(void)
{
	struct capsule_tpm_anchor_platform_request request = {
		.revision = CAPSULE_TPM_ANCHOR_PLATFORM_REQUEST_REVISION,
		.size = sizeof(request),
		.policy_revision = CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION,
		.nv_index = HR_NV_INDEX | 0x20,
		.generation = 9,
		.transaction = 17,
		.current.epoch = 3,
		.candidate.epoch = 4,
	};

	memset(request.current.digest, 0x11, sizeof(request.current.digest));
	memset(request.candidate.digest, 0x22, sizeof(request.candidate.digest));
	return request;
}

static enum cb_err run(enum initial_state state, struct provider_context *provider,
	struct backend_context *backend, struct capsule_tpm_anchor_transition *transition,
	struct tpm_pre_os_lifecycle *lifecycle,
	struct capsule_tpm_anchor_binding *binding,
	struct capsule_tpm_anchor_platform_request *request)
{
	struct capsule_tpm_anchor_value third = request->current;
	struct capsule_tpm_anchor_trusted_transition_provider callbacks = {
		.revision = CAPSULE_TPM_ANCHOR_TRUSTED_TRANSITION_PROVIDER_REVISION,
		.size = sizeof(callbacks),
		.prepared = prepared,
		.read = read_anchor,
		.install = install,
		.context = provider,
		.context_size = sizeof(*provider),
	};
	struct tpm_pre_os_backend transport = {
		.begin = begin,
		.transmit = transmit,
		.quiesce = quiesce,
		.release = release,
	};
	struct backend_ref backend_ref = { .context = backend };

	third.digest[0] ^= 0x5a;
	backend->command_count = 0;
	backend->write_count = 0;
	backend->lock_count = 0;
	backend->close_count = 0;
	backend->verify_count = 0;
	backend->candidate = request->candidate;
	backend->ph_enable = true;
	backend->value = state >= CANDIDATE_UNLOCKED && state <= CANDIDATE_LOCKED ?
		request->candidate : state == THIRD_UNLOCKED ? third : request->current;
	backend->locked = state == CURRENT_LOCKED || state == CANDIDATE_LOCKED;
	provider->backend = backend;
	provider->original = request;
	provider->transition = transition;
	provider->lifecycle = lifecycle;
	provider->binding = binding;
	provider->provider = &callbacks;
	CHECK(tpm_pre_os_lifecycle_install(lifecycle, &transport, &backend_ref,
		sizeof(backend_ref)) == CB_SUCCESS);
	return capsule_tpm_anchor_transition_run_trusted(transition, lifecycle,
		binding, request, &callbacks);
}

static void test_state_matrix(void)
{
	for (enum initial_state state = CURRENT_UNLOCKED;
	     state <= THIRD_UNLOCKED; state++) {
		struct capsule_tpm_anchor_binding binding = valid_binding();
		struct capsule_tpm_anchor_platform_request request = valid_request();
		struct capsule_tpm_anchor_transition transition = { 0 };
		struct tpm_pre_os_lifecycle lifecycle = { 0 };
		struct backend_context backend = { 0 };
		struct provider_context provider = { 0 };
		enum cb_err result = run(state, &provider, &backend, &transition,
			&lifecycle, &binding, &request);
		bool success = state == CURRENT_UNLOCKED ||
			state == CANDIDATE_UNLOCKED || state == CANDIDATE_LOCKED;

		CHECK((result == CB_SUCCESS) == success);
		CHECK(provider.prepared_count == 1 && provider.read_count >= 1);
		CHECK(provider.install_count == success);
		CHECK(backend.write_count == (state == CURRENT_UNLOCKED));
		CHECK(backend.lock_count == (state == CURRENT_UNLOCKED ||
			state == CANDIDATE_UNLOCKED));
		CHECK(backend.close_count == 1 && backend.verify_count == 1);
		if (success)
			CHECK(tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
	}
}

static void test_advisory_and_one_shot(void)
{
	struct capsule_tpm_anchor_binding binding = valid_binding();
	struct capsule_tpm_anchor_platform_request request = valid_request();
	struct capsule_tpm_anchor_transition transition = { 0 };
	struct tpm_pre_os_lifecycle lifecycle = { 0 };
	struct backend_context backend = { 0 };
	struct provider_context provider = { .reenter = true };

	CHECK(run(CURRENT_UNLOCKED, &provider, &backend, &transition, &lifecycle,
		&binding, &request) == CB_SUCCESS);
	CHECK(capsule_tpm_anchor_transition_run_trusted(&transition, &lifecycle,
		&binding, &request, NULL) == CB_ERR);

	for (unsigned int fault = 0; fault < 3; fault++) {
		binding = valid_binding();
		request = valid_request();
		transition = (struct capsule_tpm_anchor_transition) { 0 };
		lifecycle = (struct tpm_pre_os_lifecycle) { 0 };
		provider = (struct provider_context) { 0 };
		backend = (struct backend_context) {
			.advisory_write = fault == 0,
			.advisory_lock = fault == 1,
			.advisory_close = fault == 2,
		};
		CHECK(run(CURRENT_UNLOCKED, &provider, &backend, &transition,
			&lifecycle, &binding, &request) == CB_SUCCESS);
	}
}

static void test_callback_failures(void)
{
	for (unsigned int fault = 0; fault < 7; fault++) {
		struct capsule_tpm_anchor_binding binding = valid_binding();
		struct capsule_tpm_anchor_platform_request request = valid_request();
		struct capsule_tpm_anchor_transition transition = { 0 };
		struct tpm_pre_os_lifecycle lifecycle = { 0 };
		struct backend_context backend;
		struct provider_context provider = {
			.prepared_fails = fault == 0,
			.install_fails = fault == 1,
			.mutate_at = fault >= 2 && fault <= 4 ? fault - 1 : 0,
		};

		backend = (struct backend_context) {
			.transport_at = fault == 5 ? 1 : 0,
			.malformed_at = fault == 6 ? 1 : 0,
		};

		CHECK(run(CURRENT_UNLOCKED, &provider, &backend, &transition,
			&lifecycle, &binding, &request) == CB_ERR);
		CHECK(!provider.install_count || fault == 1 || fault == 4);
	}
}

static void test_transport_and_response_failures(void)
{
	for (unsigned int malformed = 0; malformed <= 1; malformed++) {
		for (unsigned int command = 1; command <= 4; command++) {
			struct capsule_tpm_anchor_binding binding = valid_binding();
			struct capsule_tpm_anchor_platform_request request =
				valid_request();
			struct capsule_tpm_anchor_transition transition = { 0 };
			struct tpm_pre_os_lifecycle lifecycle = { 0 };
			struct backend_context backend = {
				.malformed_at = malformed ? command : 0,
				.transport_at = malformed ? 0 : command,
			};
			struct provider_context provider = { 0 };

			CHECK(run(CURRENT_UNLOCKED, &provider, &backend, &transition,
				&lifecycle, &binding, &request) == CB_ERR);
			CHECK(!provider.install_count);
			CHECK(!tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
		}
	}
	{
		struct capsule_tpm_anchor_binding binding = valid_binding();
		struct capsule_tpm_anchor_platform_request request = valid_request();
		struct capsule_tpm_anchor_transition transition = { 0 };
		struct tpm_pre_os_lifecycle lifecycle = { 0 };
		struct backend_context backend = { .write_locks = true };
		struct provider_context provider = { 0 };

		CHECK(run(CURRENT_UNLOCKED, &provider, &backend, &transition,
			&lifecycle, &binding, &request) == CB_ERR);
		CHECK(!provider.install_count);
	}
}

int main(void)
{
	test_state_matrix();
	test_advisory_and_one_shot();
	test_callback_failures();
	test_transport_and_response_failures();
	return 0;
}
