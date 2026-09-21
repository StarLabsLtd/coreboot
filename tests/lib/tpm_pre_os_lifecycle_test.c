/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/pre_os_lifecycle.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

enum callback_action {
	ACTION_NONE,
	ACTION_ACQUIRE,
	ACTION_TRANSMIT,
	ACTION_HANDOFF,
	ACTION_MUTATE_BACKEND,
	ACTION_MUTATE_CONTEXT_SIZE,
	ACTION_MUTATE_CONTROL,
	ACTION_MUTATE_GENERATION,
	ACTION_MUTATE_TOKEN,
	ACTION_BLOCK,
};

struct test_environment {
	struct tpm_pre_os_lifecycle *lifecycle;
	struct tpm_pre_os_token *token;
	enum cb_err begin_result;
	enum cb_err transmit_result;
	enum cb_err quiesce_result;
	enum cb_err release_result;
	enum callback_action begin_action;
	enum callback_action transmit_action;
	enum callback_action quiesce_action;
	enum callback_action release_action;
	char events[32];
	size_t event_count;
	bool empty_response;
	bool oversized_response;
	bool callback_entered;
	bool release_callback;
};

struct backend_context {
	struct test_environment *environment;
};

static void record(struct test_environment *environment, char event)
{
	CHECK(environment->event_count < sizeof(environment->events));
	environment->events[environment->event_count++] = event;
}

static void reenter(struct test_environment *environment,
	enum callback_action action)
{
	uint8_t request = 0x80;
	uint8_t response[4] = { 0xa5, 0xa5, 0xa5, 0xa5 };
	size_t response_size = sizeof(response);
	struct tpm_pre_os_token token;

	switch (action) {
	case ACTION_NONE:
		break;
	case ACTION_ACQUIRE:
		memset(&token, 0xa5, sizeof(token));
		CHECK(tpm_pre_os_lifecycle_acquire(environment->lifecycle,
			&token) == CB_ERR);
		CHECK(token.lifecycle == 0 && token.generation == 0);
		break;
	case ACTION_TRANSMIT:
		CHECK(tpm_pre_os_lifecycle_transmit(environment->lifecycle,
			environment->token, &request, sizeof(request), response,
			&response_size) == CB_ERR);
		CHECK(response_size == 0);
		break;
	case ACTION_HANDOFF:
		CHECK(tpm_pre_os_lifecycle_handoff(environment->lifecycle) ==
			CB_ERR);
		break;
	case ACTION_MUTATE_BACKEND:
		environment->lifecycle->backend.release = NULL;
		break;
	case ACTION_MUTATE_CONTEXT_SIZE:
		environment->lifecycle->context_size++;
		break;
	case ACTION_MUTATE_CONTROL:
		environment->lifecycle->control ^= 1;
		break;
	case ACTION_MUTATE_GENERATION:
		environment->lifecycle->generation++;
		break;
	case ACTION_MUTATE_TOKEN:
		environment->token->generation++;
		break;
	case ACTION_BLOCK:
		__atomic_store_n(&environment->callback_entered, true,
			__ATOMIC_RELEASE);
		while (!__atomic_load_n(&environment->release_callback,
			__ATOMIC_ACQUIRE))
			sched_yield();
		break;
	}
}

static enum cb_err fake_begin(void *context)
{
	struct test_environment *environment =
		((struct backend_context *)context)->environment;

	record(environment, 'B');
	reenter(environment, environment->begin_action);
	return environment->begin_result;
}

static enum cb_err fake_transmit(void *context, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size)
{
	struct test_environment *environment =
		((struct backend_context *)context)->environment;

	record(environment, 'T');
	CHECK(request && request_size);
	reenter(environment, environment->transmit_action);
	if (*response_size >= 3) {
		response[0] = 0x80;
		response[1] = 0x01;
		response[2] = 0x00;
	}
	if (environment->empty_response)
		*response_size = 0;
	else if (environment->oversized_response)
		(*response_size)++;
	else
		*response_size = 3;
	return environment->transmit_result;
}

static enum cb_err fake_quiesce(void *context)
{
	struct test_environment *environment =
		((struct backend_context *)context)->environment;

	record(environment, 'Q');
	reenter(environment, environment->quiesce_action);
	return environment->quiesce_result;
}

static enum cb_err fake_release(void *context)
{
	struct test_environment *environment =
		((struct backend_context *)context)->environment;

	record(environment, 'R');
	reenter(environment, environment->release_action);
	return environment->release_result;
}

static const struct tpm_pre_os_backend backend = {
	.begin = fake_begin,
	.transmit = fake_transmit,
	.quiesce = fake_quiesce,
	.release = fake_release,
};

static void reset(struct test_environment *environment,
	struct tpm_pre_os_lifecycle *lifecycle,
	struct tpm_pre_os_token *token)
{
	memset(environment, 0, sizeof(*environment));
	memset(lifecycle, 0, sizeof(*lifecycle));
	memset(token, 0, sizeof(*token));
	environment->lifecycle = lifecycle;
	environment->token = token;
}

static void install(struct test_environment *environment,
	struct tpm_pre_os_lifecycle *lifecycle)
{
	struct backend_context context = { .environment = environment };

	CHECK(tpm_pre_os_lifecycle_install(lifecycle, &backend, &context,
		sizeof(context)) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_state(lifecycle) == TPM_PRE_OS_AVAILABLE);
	CHECK(!tpm_pre_os_lifecycle_os_access_allowed(lifecycle));
}

static void assert_zero(const void *data, size_t size)
{
	const uint8_t *bytes = data;

	for (size_t i = 0; i < size; i++)
		CHECK(bytes[i] == 0);
}

enum async_operation {
	ASYNC_INSTALL,
	ASYNC_ACQUIRE,
	ASYNC_TRANSMIT,
	ASYNC_HANDOFF,
};

enum callback_slot {
	CALLBACK_BEGIN,
	CALLBACK_TRANSMIT,
	CALLBACK_QUIESCE,
	CALLBACK_RELEASE,
};

struct async_call {
	enum async_operation operation;
	struct test_environment *environment;
	struct tpm_pre_os_lifecycle *lifecycle;
	struct tpm_pre_os_token *token;
	enum cb_err result;
	uint8_t response[8];
	size_t response_size;
	bool start;
	pthread_barrier_t *barrier;
};

static void *run_async(void *argument)
{
	struct async_call *call = argument;
	struct backend_context context = {
		.environment = call->environment,
	};
	uint8_t request = 0x80;

	if (call->barrier) {
		int result = pthread_barrier_wait(call->barrier);

		CHECK(!result || result == PTHREAD_BARRIER_SERIAL_THREAD);
	} else {
		while (!__atomic_load_n(&call->start, __ATOMIC_ACQUIRE))
			sched_yield();
	}
	switch (call->operation) {
	case ASYNC_INSTALL:
		call->result = tpm_pre_os_lifecycle_install(call->lifecycle,
			&backend, &context, sizeof(context));
		break;
	case ASYNC_ACQUIRE:
		call->result = tpm_pre_os_lifecycle_acquire(call->lifecycle,
			call->token);
		break;
	case ASYNC_TRANSMIT:
		call->response_size = sizeof(call->response);
		call->result = tpm_pre_os_lifecycle_transmit(call->lifecycle,
			call->token, &request, sizeof(request), call->response,
			&call->response_size);
		break;
	case ASYNC_HANDOFF:
		call->result = tpm_pre_os_lifecycle_handoff(call->lifecycle);
		break;
	}
	return NULL;
}

static void start_async(pthread_t *thread, struct async_call *call)
{
	CHECK(!pthread_create(thread, NULL, run_async, call));
	__atomic_store_n(&call->start, true, __ATOMIC_RELEASE);
}

static void wait_for_callback(struct test_environment *environment)
{
	for (size_t i = 0; i < 1000000; i++) {
		if (__atomic_load_n(&environment->callback_entered,
			__ATOMIC_ACQUIRE))
			return;
		sched_yield();
	}
	CHECK(false);
}

static void release_callback(struct test_environment *environment)
{
	__atomic_store_n(&environment->release_callback, true,
		__ATOMIC_RELEASE);
}

static void test_success(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	uint8_t request[] = { 0x80, 0x01 };
	uint8_t response[8];
	size_t response_size;

	reset(&environment, &lifecycle, &token);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_UNBOUND);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(token.lifecycle == (uintptr_t)&lifecycle && token.generation == 1);
	memset(response, 0xa5, sizeof(response));
	response_size = sizeof(response);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, request,
		sizeof(request), response, &response_size) == CB_SUCCESS);
	CHECK(response_size == 3 && response[0] == 0x80 &&
		response[1] == 0x01 && response[2] == 0x00);
	CHECK(tpm_pre_os_lifecycle_end(&lifecycle, &token) == CB_SUCCESS);
	assert_zero(&token, sizeof(token));
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(token.generation == 2);
	CHECK(tpm_pre_os_lifecycle_end(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_HANDED_OFF);
	CHECK(tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
	CHECK(environment.event_count == 4);
	CHECK(!memcmp(environment.events, "BTQR", 4));
	assert_zero(&lifecycle.backend, sizeof(lifecycle.backend));
	assert_zero(lifecycle.backend_context,
		sizeof(lifecycle.backend_context));
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
	memset(&token, 0xa5, sizeof(token));
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_ERR);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) ==
		TPM_PRE_OS_HANDED_OFF);
	CHECK(tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, request,
		sizeof(request), response, NULL) == CB_ERR_ARG);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) ==
		TPM_PRE_OS_HANDED_OFF);
	CHECK(tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
	lifecycle.backend_context[0] = 1;
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) ==
		TPM_PRE_OS_HANDED_OFF);
	CHECK(!tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
}

static void test_install_failures(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	struct backend_context context = { .environment = &environment };
	struct tpm_pre_os_backend malformed;

	reset(&environment, &lifecycle, &token);
	CHECK(tpm_pre_os_lifecycle_install(NULL, &backend, &context,
		sizeof(context)) == CB_ERR_ARG);
	CHECK(tpm_pre_os_lifecycle_install(&lifecycle, NULL, NULL, 0) ==
		CB_ERR_ARG);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	for (size_t i = 0; i < 4; i++) {
		reset(&environment, &lifecycle, &token);
		malformed = backend;
		switch (i) {
		case 0:
			malformed.begin = NULL;
			break;
		case 1:
			malformed.transmit = NULL;
			break;
		case 2:
			malformed.quiesce = NULL;
			break;
		default:
			malformed.release = NULL;
			break;
		}
		CHECK(tpm_pre_os_lifecycle_install(&lifecycle, &malformed,
			&context, sizeof(context)) == CB_ERR_ARG);
		CHECK(tpm_pre_os_lifecycle_state(&lifecycle) ==
			TPM_PRE_OS_FAILED);
	}

	reset(&environment, &lifecycle, &token);
	CHECK(tpm_pre_os_lifecycle_install(&lifecycle, &backend, NULL,
		sizeof(context)) == CB_ERR_ARG);
	reset(&environment, &lifecycle, &token);
	CHECK(tpm_pre_os_lifecycle_install(&lifecycle, &backend, &context,
		TPM_PRE_OS_BACKEND_CONTEXT_SIZE + 1) == CB_ERR_ARG);

	reset(&environment, &lifecycle, &token);
	environment.begin_result = CB_ERR;
	CHECK(tpm_pre_os_lifecycle_install(&lifecycle, &backend, &context,
		sizeof(context)) == CB_ERR);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	CHECK(environment.event_count == 1 && environment.events[0] == 'B');

	reset(&environment, &lifecycle, &token);
	environment.begin_action = ACTION_ACQUIRE;
	CHECK(tpm_pre_os_lifecycle_install(&lifecycle, &backend, &context,
		sizeof(context)) == CB_ERR);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_install(&lifecycle, &backend, &context,
		sizeof(context)) == CB_ERR);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

}

static void test_tokens_and_generation(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	struct tpm_pre_os_token stale;
	uint8_t response[8];
	size_t response_size;

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	stale = token;
	CHECK(tpm_pre_os_lifecycle_end(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_end(&lifecycle, &stale) == CB_ERR);
	assert_zero(&stale, sizeof(stale));
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	lifecycle.generation = UINT64_MAX;
	memset(&token, 0xa5, sizeof(token));
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_ERR);
	assert_zero(&token, sizeof(token));
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	memset(response, 0xa5, sizeof(response));
	response_size = sizeof(response);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token,
		(const uint8_t *)(uintptr_t)1, (uintptr_t)-1, response,
		&response_size) == CB_ERR_ARG);
	CHECK(response_size == 0);
	assert_zero(response, sizeof(response));
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
	CHECK(environment.event_count == 1);
}

static void test_explicit_failure(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_fail(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
	CHECK(!tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
	assert_zero(&lifecycle.backend, sizeof(lifecycle.backend));
	assert_zero(lifecycle.backend_context,
		sizeof(lifecycle.backend_context));
	CHECK(tpm_pre_os_lifecycle_end(&lifecycle, &token) == CB_ERR);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_fail(&lifecycle, NULL) == CB_ERR_ARG);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	token.generation++;
	CHECK(tpm_pre_os_lifecycle_fail(&lifecycle, &token) == CB_ERR);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	CHECK(tpm_pre_os_lifecycle_fail(NULL, &token) == CB_ERR_ARG);
}

static void test_transmit_failures(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	uint8_t request = 0x80;
	uint8_t response[8];
	size_t response_size;

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	environment.transmit_result = CB_ERR;
	memset(response, 0xa5, sizeof(response));
	response_size = sizeof(response);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, &request,
		sizeof(request), response, &response_size) == CB_ERR);
	CHECK(response_size == 0);
	assert_zero(response, sizeof(response));
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	environment.oversized_response = true;
	memset(response, 0xa5, sizeof(response));
	response_size = 2;
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, &request,
		sizeof(request), response, &response_size) == CB_ERR);
	CHECK(response_size == 0 && response[0] == 0 && response[1] == 0);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	environment.empty_response = true;
	memset(response, 0xa5, sizeof(response));
	response_size = sizeof(response);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, &request,
		sizeof(request), response, &response_size) == CB_ERR);
	assert_zero(response, sizeof(response));
}

static void test_transmit_arguments(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_lifecycle other;
	struct tpm_pre_os_token token;
	struct tpm_pre_os_token other_token;
	uint8_t request = 0x80;
	uint8_t response[8];
	size_t response_size;

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	memset(response, 0xa5, sizeof(response));
	response_size = sizeof(response);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, NULL, 0,
		response, &response_size) == CB_ERR_ARG);
	CHECK(response_size == 0);
	assert_zero(response, sizeof(response));
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	memset(response, 0xa5, sizeof(response));
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, &request,
		sizeof(request), response, NULL) == CB_ERR_ARG);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	memset(response, 0xa5, sizeof(response));
	response_size = 0;
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, &request,
		sizeof(request), response, &response_size) == CB_ERR_ARG);
	CHECK(response_size == 0);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	memset(response, 0xa5, sizeof(response));
	response_size = sizeof(response);
	other_token = token;
	other_token.generation++;
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &other_token, &request,
		sizeof(request), response, &response_size) == CB_ERR);
	CHECK(response_size == 0);
	assert_zero(response, sizeof(response));
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	memset(&other, 0, sizeof(other));
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	other = lifecycle;
	other_token = token;
	other_token.lifecycle = (uintptr_t)&other;
	memset(response, 0xa5, sizeof(response));
	response_size = sizeof(response);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &other_token, &request,
		sizeof(request), response, &response_size) == CB_ERR);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
}

static void test_object_aliases_and_corruption(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	struct backend_context context = { .environment = &environment };
	uint8_t response[8];
	size_t response_size;
	uint32_t corruption;

	reset(&environment, &lifecycle, &token);
	CHECK(tpm_pre_os_lifecycle_install(&lifecycle, &backend, &lifecycle,
		sizeof(context)) == CB_ERR_ARG);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_end(&lifecycle,
		(struct tpm_pre_os_token *)&lifecycle) == CB_ERR_ARG);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token,
		(const uint8_t *)"x", 1, response,
		(size_t *)&lifecycle.context_size) == CB_ERR_ARG);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle,
		(struct tpm_pre_os_token *)&lifecycle) == CB_ERR_ARG);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	response_size = sizeof(response);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token,
		(const uint8_t *)&lifecycle, 1, response, &response_size) ==
		CB_ERR_ARG);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	response_size = 1;
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token,
		(const uint8_t *)"x", 1, (uint8_t *)&lifecycle,
		&response_size) == CB_ERR_ARG);
	CHECK(response_size == 0);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	lifecycle.revision ^= 1;
	memset(&token, 0xa5, sizeof(token));
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_ERR);
	assert_zero(&token, sizeof(token));
	lifecycle.revision ^= 1;
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	corruption = 1U << 16;
	lifecycle.control ^= corruption;
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
	memset(&token, 0xa5, sizeof(token));
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_ERR);
	lifecycle.control ^= corruption;
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
}

static void test_backend_is_copied(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	struct backend_context context = { .environment = &environment };
	struct tpm_pre_os_backend mutable_backend = backend;

	reset(&environment, &lifecycle, &token);
	CHECK(tpm_pre_os_lifecycle_install(&lifecycle, &mutable_backend,
		&context, sizeof(context)) == CB_SUCCESS);
	memset(&mutable_backend, 0, sizeof(mutable_backend));
	memset(&context, 0, sizeof(context));
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_SUCCESS);
	CHECK(environment.event_count == 3);
	CHECK(!memcmp(environment.events, "BQR", 3));
}

static void test_reentry(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	uint8_t request = 0x80;
	uint8_t response[8];
	size_t response_size = sizeof(response);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	environment.transmit_action = ACTION_TRANSMIT;
	memset(response, 0xa5, sizeof(response));
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, &request,
		sizeof(request), response, &response_size) == CB_ERR);
	assert_zero(response, sizeof(response));
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	environment.transmit_action = ACTION_MUTATE_BACKEND;
	response_size = sizeof(response);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, &request,
		sizeof(request), response, &response_size) == CB_ERR);
	CHECK(response_size == 0);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	environment.quiesce_action = ACTION_ACQUIRE;
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(environment.event_count == 2);
	CHECK(!memcmp(environment.events, "BQ", 2));
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
}

static void test_handoff_failures(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	environment.quiesce_result = CB_ERR;
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(environment.event_count == 2);
	CHECK(!memcmp(environment.events, "BQ", 2));
	CHECK(!tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	environment.release_result = CB_ERR;
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(environment.event_count == 3);
	CHECK(!memcmp(environment.events, "BQR", 3));
	CHECK(!tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	environment.release_action = ACTION_ACQUIRE;
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	environment.quiesce_action = ACTION_MUTATE_BACKEND;
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(environment.event_count == 2);
	CHECK(!memcmp(environment.events, "BQ", 2));
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
}

static void test_callback_metadata_mutation(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	uint8_t request = 0x80;
	uint8_t response[8];
	size_t response_size;

	reset(&environment, &lifecycle, &token);
	environment.begin_action = ACTION_MUTATE_GENERATION;
	{
		struct backend_context context = { .environment = &environment };

		CHECK(tpm_pre_os_lifecycle_install(&lifecycle, &backend, &context,
			sizeof(context)) == CB_ERR);
	}
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	environment.begin_action = ACTION_MUTATE_CONTEXT_SIZE;
	{
		struct backend_context context = { .environment = &environment };

		CHECK(tpm_pre_os_lifecycle_install(&lifecycle, &backend, &context,
			sizeof(context)) == CB_ERR);
	}

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	environment.transmit_action = ACTION_MUTATE_TOKEN;
	response_size = sizeof(response);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, &request,
		sizeof(request), response, &response_size) == CB_ERR);
	CHECK(response_size == 0);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	environment.quiesce_action = ACTION_MUTATE_GENERATION;
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(environment.event_count == 2);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	environment.release_action = ACTION_MUTATE_CONTEXT_SIZE;
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(environment.event_count == 3);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	environment.quiesce_action = ACTION_MUTATE_CONTROL;
	CHECK(tpm_pre_os_lifecycle_handoff(&lifecycle) == CB_ERR);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
}

static void test_token_aliases(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	struct tpm_pre_os_token saved;
	uint8_t request = 0x80;
	size_t response_size;

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	saved = token;
	response_size = sizeof(token);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, &request,
		sizeof(request), (uint8_t *)&token, &response_size) ==
		CB_ERR_ARG);
	CHECK(response_size == 0);
	CHECK(!memcmp(&token, &saved, sizeof(token)));
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);

	reset(&environment, &lifecycle, &token);
	install(&environment, &lifecycle);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_transmit(&lifecycle, &token, &request,
		sizeof(request), (uint8_t *)&saved,
		(size_t *)&token.generation) == CB_ERR_ARG);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
}

static void parallel_acquire_once(void)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	struct tpm_pre_os_token other_token;
	struct async_call first;
	struct async_call second;
	pthread_t first_thread;
	pthread_t second_thread;
	pthread_barrier_t barrier;
	unsigned int successes;

	reset(&environment, &lifecycle, &token);
	memset(&other_token, 0, sizeof(other_token));
	install(&environment, &lifecycle);
	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	first.operation = ASYNC_ACQUIRE;
	first.lifecycle = &lifecycle;
	first.token = &token;
	first.barrier = &barrier;
	second.operation = ASYNC_ACQUIRE;
	second.lifecycle = &lifecycle;
	second.token = &other_token;
	second.barrier = &barrier;
	CHECK(!pthread_barrier_init(&barrier, NULL, 3));
	CHECK(!pthread_create(&first_thread, NULL, run_async, &first));
	CHECK(!pthread_create(&second_thread, NULL, run_async, &second));
	{
		int result = pthread_barrier_wait(&barrier);

		CHECK(!result || result == PTHREAD_BARRIER_SERIAL_THREAD);
	}
	CHECK(!pthread_join(first_thread, NULL));
	CHECK(!pthread_join(second_thread, NULL));
	CHECK(!pthread_barrier_destroy(&barrier));
	successes = (first.result == CB_SUCCESS) +
		(second.result == CB_SUCCESS);
	CHECK(successes <= 1);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
}

static void test_parallel_acquire(void)
{
	for (size_t i = 0; i < 256; i++)
		parallel_acquire_once();
}

static void test_callback_contention(enum async_operation operation,
	enum callback_slot slot)
{
	struct test_environment environment;
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	struct tpm_pre_os_token contender;
	struct async_call call;
	pthread_t thread;

	reset(&environment, &lifecycle, &token);
	memset(&contender, 0, sizeof(contender));
	memset(&call, 0, sizeof(call));
	call.operation = operation;
	call.environment = &environment;
	call.lifecycle = &lifecycle;
	call.token = &token;
	if (operation != ASYNC_INSTALL)
		install(&environment, &lifecycle);
	if (operation == ASYNC_TRANSMIT)
		CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &token) ==
			CB_SUCCESS);
	switch (slot) {
	case CALLBACK_BEGIN:
		environment.begin_action = ACTION_BLOCK;
		break;
	case CALLBACK_TRANSMIT:
		environment.transmit_action = ACTION_BLOCK;
		break;
	case CALLBACK_QUIESCE:
		environment.quiesce_action = ACTION_BLOCK;
		break;
	case CALLBACK_RELEASE:
		environment.release_action = ACTION_BLOCK;
		break;
	}
	start_async(&thread, &call);
	wait_for_callback(&environment);
	if (operation == ASYNC_HANDOFF)
		CHECK(tpm_pre_os_lifecycle_state(&lifecycle) ==
			TPM_PRE_OS_HANDING_OFF);
	else
		CHECK(tpm_pre_os_lifecycle_state(&lifecycle) ==
			TPM_PRE_OS_BUSY);
	CHECK(!tpm_pre_os_lifecycle_os_access_allowed(&lifecycle));
	CHECK(lifecycle.backend.begin && lifecycle.backend.release);
	CHECK(lifecycle.context_size != 0);
	CHECK(tpm_pre_os_lifecycle_acquire(&lifecycle, &contender) == CB_ERR);
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
	CHECK(lifecycle.backend.begin && lifecycle.backend.release);
	CHECK(lifecycle.context_size != 0);
	release_callback(&environment);
	CHECK(!pthread_join(thread, NULL));
	CHECK(call.result == CB_ERR);
	if (operation == ASYNC_TRANSMIT) {
		CHECK(call.response_size == 0);
		assert_zero(call.response, sizeof(call.response));
	}
	CHECK(tpm_pre_os_lifecycle_state(&lifecycle) == TPM_PRE_OS_FAILED);
	assert_zero(&lifecycle.backend, sizeof(lifecycle.backend));
	assert_zero(lifecycle.backend_context,
		sizeof(lifecycle.backend_context));
}

static void test_callback_contention_all(void)
{
	test_callback_contention(ASYNC_INSTALL, CALLBACK_BEGIN);
	test_callback_contention(ASYNC_TRANSMIT, CALLBACK_TRANSMIT);
	test_callback_contention(ASYNC_HANDOFF, CALLBACK_QUIESCE);
	test_callback_contention(ASYNC_HANDOFF, CALLBACK_RELEASE);
}

int main(void)
{
	test_success();
	test_install_failures();
	test_tokens_and_generation();
	test_explicit_failure();
	test_transmit_failures();
	test_transmit_arguments();
	test_object_aliases_and_corruption();
	test_backend_is_copied();
	test_reentry();
	test_handoff_failures();
	test_callback_metadata_mutation();
	test_token_aliases();
	test_parallel_acquire();
	test_callback_contention_all();
	return 0;
}
