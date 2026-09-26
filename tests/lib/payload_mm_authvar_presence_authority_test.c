/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_service.h>
#include <bootmem.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static union {
	struct payload_mm_authvar_presence_message message;
	uint8_t bytes[PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE];
} mailbox_page __aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT);
#define mailbox mailbox_page.message
static uint8_t provisioned[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE];
static uint64_t executor_status;
static bool executor_reset_required;
static unsigned int executor_calls;
static unsigned int reset_calls;
static bool dma_ok;
static bool rendezvous_ok;
static bool provision_zero;
static bool reset_returned;
static bool mailbox_is_protected;
static bool corrupt_on_dma;
static bool mutate_context_on_provision;
static bool mutate_context_on_dma;
static unsigned int mutate_context_on_dma_call;
static unsigned int dma_calls;
static bool provision_mailbox_then_fail;
static bool revoke_dma_on_executor;
static bool revoke_rendezvous_on_executor;
static bool backing_proof_ok;
static unsigned int backing_proof_calls;
static bool executor_block;
static bool executor_entered;
static bool executor_release;
static bool cleanup_observed;
static bool cleanup_page_zero;
static unsigned int cleanup_calls;
static enum cb_err dispatch_thread_result;

struct callback_context {
	uint32_t magic;
};

static struct callback_context callback_context;

static bool state_contains_capability(void);

static pthread_mutex_t restrict_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t restrict_cond = PTHREAD_COND_INITIALIZER;
static bool restrict_hook_entered;
static bool restrict_hook_release;
static enum cb_err restrict_thread_result;

static bool test_bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0U;

	while (size--)
		value |= *bytes++;
	return value == 0U;
}

uint64_t payload_mm_authvar_executor_enter_setup_mode(bool *reset_required)
{
	executor_calls++;
	assert(!pthread_mutex_lock(&restrict_mutex));
	executor_entered = true;
	assert(!pthread_cond_broadcast(&restrict_cond));
	while (executor_block && !executor_release)
		assert(!pthread_cond_wait(&restrict_cond, &restrict_mutex));
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(reset_required);
	*reset_required = executor_reset_required;
	if (revoke_dma_on_executor)
		dma_ok = false;
	if (revoke_rendezvous_on_executor)
		rendezvous_ok = false;
	return executor_status;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	const uintptr_t start = (uintptr_t)storage;
	const uintptr_t mailbox_start = (uintptr_t)&mailbox_page;

	(void)context;
	if (mailbox_is_protected)
		return storage != NULL && size != 0U;
	return storage != NULL && size != 0U &&
		(start > mailbox_start || mailbox_start - start >= size) &&
		(mailbox_start > start || start - mailbox_start >= sizeof(mailbox_page));
}

static enum cb_err provision(void *context, uint64_t generation,
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE])
{
	struct callback_context *value = context;

	assert(value && value->magic == 0x13579bdfU);
	if (mutate_context_on_provision)
		value->magic++;
	assert(generation == 7U);
	memset(capability, 0, LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE);
	if (!provision_zero)
		for (size_t index = 0; index < sizeof(provisioned); index++)
			capability[index] = (uint8_t)(index + 1U);
	memcpy(provisioned, capability, sizeof(provisioned));
	if (provision_mailbox_then_fail) {
		memcpy(&mailbox, capability,
			LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static bool dma_protected(void *context, uint64_t base, uint64_t size)
{
	struct callback_context *value = context;

	dma_calls++;
	assert(value && value->magic == 0x13579bdfU);
	if (!test_bytes_zero(provisioned, sizeof(provisioned)))
		assert(!state_contains_capability());
	if (mutate_context_on_dma || dma_calls == mutate_context_on_dma_call)
		value->magic++;
	if (corrupt_on_dma) {
		size_t authority_size;
		uint8_t *authority = (void *)(uintptr_t)
			payload_mm_authvar_presence_authority_test_state(&authority_size);

		assert(authority_size != 0U);
		authority[0] ^= 1U;
		corrupt_on_dma = false;
	}
	return dma_ok && base == (uintptr_t)&mailbox_page &&
		size == sizeof(mailbox_page);
}

static bool rendezvous(void *context)
{
	struct callback_context *value = context;

	assert(value && value->magic == 0x13579bdfU);
	return rendezvous_ok;
}

enum cb_err payload_mm_authvar_presence_backing_evidence_take(
	const struct payload_mm_authvar_presence_backing *backing)
{
	backing_proof_calls++;
	return backing_proof_ok &&
		backing && backing->base == (uintptr_t)&mailbox_page &&
		backing->bytes == sizeof(mailbox_page) &&
		backing->generation == 7U && backing->tag == BM_MEM_RESERVED ?
		CB_SUCCESS : CB_ERR;
}

static void cold_reset(void *context)
{
	struct callback_context *value = context;

	assert(value && value->magic == 0x13579bdfU);
	reset_calls++;
	reset_returned = true;
}

static __noreturn void fail_stop(void *context)
{
	struct callback_context *value = context;

	assert(value && value->magic == 0x13579bdfU);
	_exit(77);
}

static void assert_fail_stopped(int status)
{
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 77);
}

static struct payload_mm_authvar_presence_policy policy(void)
{
	return (struct payload_mm_authvar_presence_policy) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_POLICY_REVISION,
		.size = sizeof(struct payload_mm_authvar_presence_policy),
		.endpoint = {
			.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
			.size = sizeof(struct lb_authvar_presence_endpoint),
			.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
			.header_size = sizeof(struct lb_authvar_presence_endpoint),
			.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS,
			.generation = 7,
			.communication_base = (uintptr_t)&mailbox_page,
			.communication_size = sizeof(mailbox),
			.message_size = sizeof(mailbox),
			.transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
			.trigger_width = 1,
			.trigger_address = 0xb2,
			.trigger_value = 0xe8,
			.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
			.capability_size = LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
		},
		.backing = {
			.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION,
			.size = sizeof(struct payload_mm_authvar_presence_backing),
			.base = (uintptr_t)&mailbox_page,
			.bytes = sizeof(mailbox_page),
			.generation = 7U,
			.tag = BM_MEM_RESERVED,
		},
		.provision = provision,
		.dma_protected = dma_protected,
		.cpu_rendezvous_active = rendezvous,
		.cold_reset = cold_reset,
		.fail_stop = fail_stop,
		.context = &callback_context,
		.context_size = sizeof(callback_context),
	};
}

static void make_request(void)
{
	mailbox = (struct payload_mm_authvar_presence_message) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_REVISION,
		.size = sizeof(mailbox),
		.action = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
		.generation = 7,
		.request_id = 9,
		.status = PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_PENDING,
		.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING,
	};
	memcpy(mailbox.capability, provisioned, sizeof(mailbox.capability));
}

static void reset_fixture(void)
{
	payload_mm_authvar_presence_authority_reset_test();
	memset(&mailbox_page, 0, sizeof(mailbox_page));
	memset(provisioned, 0, sizeof(provisioned));
	executor_status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	executor_reset_required = false;
	executor_calls = 0;
	reset_calls = 0;
	dma_ok = true;
	rendezvous_ok = true;
	provision_zero = false;
	reset_returned = false;
	mailbox_is_protected = false;
	corrupt_on_dma = false;
	mutate_context_on_provision = false;
	mutate_context_on_dma = false;
	mutate_context_on_dma_call = 0U;
	dma_calls = 0U;
	provision_mailbox_then_fail = false;
	revoke_dma_on_executor = false;
	revoke_rendezvous_on_executor = false;
	backing_proof_ok = true;
	backing_proof_calls = 0U;
	executor_block = false;
	executor_entered = false;
	executor_release = false;
	cleanup_observed = false;
	cleanup_page_zero = false;
	cleanup_calls = 0U;
	dispatch_thread_result = CB_SUCCESS;
	callback_context.magic = 0x13579bdfU;
}

static void install(void)
{
	struct payload_mm_authvar_presence_policy value = policy();

	assert(payload_mm_authvar_presence_authority_install(&value,
		protected_storage, NULL) == CB_SUCCESS);
}

static bool state_contains_capability(void)
{
	const uint8_t *state;
	size_t size;

	state = payload_mm_authvar_presence_authority_test_state(&size);
	for (size_t offset = 0; offset + sizeof(provisioned) <= size; offset++)
		if (!memcmp(state + offset, provisioned, sizeof(provisioned)))
			return true;
	return false;
}

static bool state_contains(const void *value, size_t value_size)
{
	const uint8_t *state;
	size_t state_size;

	state = payload_mm_authvar_presence_authority_test_state(&state_size);
	for (size_t offset = 0; offset + value_size <= state_size; offset++)
		if (!memcmp(state + offset, value, value_size))
			return true;
	return false;
}

static void corrupt_generation(void)
{
	uint8_t *state;
	size_t size;
	const uint64_t generation = 7U;
	bool changed = false;

	state = (void *)(uintptr_t)
		payload_mm_authvar_presence_authority_test_state(&size);
	for (size_t offset = 0; offset + sizeof(generation) <= size; offset++) {
		if (memcmp(state + offset, &generation, sizeof(generation)))
			continue;
		state[offset] ^= 1U;
		changed = true;
		offset += sizeof(generation) - 1U;
	}
	assert(changed);
}

static uint8_t *authority_generation_tail(void)
{
	const size_t tail_size = 5U * sizeof(uint64_t) + 2U * sizeof(uint32_t);
	uint8_t *state;
	size_t size;

	state = (void *)(uintptr_t)
		payload_mm_authvar_presence_authority_test_state(&size);
	assert(size >= tail_size);
	return state + size - tail_size;
}

static uint64_t tail_generation(size_t index)
{
	uint64_t value;

	assert(index < 5U);
	memcpy(&value, authority_generation_tail() + index * sizeof(value),
		sizeof(value));
	return value;
}

static void set_tail_generation(size_t index, uint64_t value)
{
	assert(index < 5U);
	memcpy(authority_generation_tail() + index * sizeof(value), &value,
		sizeof(value));
}

static void clear_install_gate(void)
{
	const uint32_t value = 0;
	uint8_t *tail = authority_generation_tail();

	memcpy(tail + 5U * sizeof(uint64_t) + sizeof(uint32_t), &value,
		sizeof(value));
}

static void reenter_restrict(void)
{
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);
}

static void block_restrict(void)
{
	assert(!pthread_mutex_lock(&restrict_mutex));
	restrict_hook_entered = true;
	assert(!pthread_cond_broadcast(&restrict_cond));
	while (!restrict_hook_release)
		assert(!pthread_cond_wait(&restrict_cond, &restrict_mutex));
	assert(!pthread_mutex_unlock(&restrict_mutex));
}

static void *restrict_thread(void *unused)
{
	(void)unused;
	restrict_thread_result =
		payload_mm_authvar_presence_authority_restrict(7U);
	return NULL;
}

static void *dispatch_thread(void *unused)
{
	(void)unused;
	dispatch_thread_result =
		payload_mm_authvar_presence_authority_dispatch();
	return NULL;
}

static void block_restrict_claim_once(void)
{
	payload_mm_authvar_presence_authority_restrict_claim_test_hook(NULL);
	block_restrict();
}

static void block_dispatch_finish_once(void)
{
	payload_mm_authvar_presence_authority_dispatch_finish_test_hook(NULL);
	block_restrict();
}

static void observe_cleanup(void)
{
	cleanup_observed = true;
	cleanup_calls++;
	cleanup_page_zero = test_bytes_zero(&mailbox_page,
		sizeof(mailbox_page));
}

static void install_validation(void)
{
	struct payload_mm_authvar_presence_policy value;
	pid_t child;
	int status;

	reset_fixture();
	value = policy();
	assert(payload_mm_authvar_presence_authority_install(&value,
		protected_storage, NULL) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_authority_install(&value,
		protected_storage, NULL) == CB_ERR);
	reset_fixture();
	provision_mailbox_then_fail = true;
	value = policy();
	assert(payload_mm_authvar_presence_authority_install(&value,
		protected_storage, NULL) == CB_ERR);
	assert(test_bytes_zero(&mailbox, sizeof(mailbox)));
	assert(!state_contains(&callback_context, sizeof(callback_context)));
	reset_fixture();
	provision_zero = true;
	value = policy();
	assert(payload_mm_authvar_presence_authority_install(&value,
		protected_storage, NULL) == CB_ERR);
	assert(!state_contains(&callback_context, sizeof(callback_context)));
	reset_fixture();
	value = policy();
	value.revision++;
	assert(payload_mm_authvar_presence_authority_install(&value,
		protected_storage, NULL) == CB_ERR);
	reset_fixture();
	mailbox_is_protected = true;
	value = policy();
	assert(payload_mm_authvar_presence_authority_install(&value,
		protected_storage, NULL) == CB_ERR);
	reset_fixture();
	mutate_context_on_provision = true;
	value = policy();
	child = fork();
	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_authority_install(&value,
			protected_storage, NULL);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert_fail_stopped(status);
	reset_fixture();
	value = policy();
	assert(payload_mm_authvar_presence_authority_install(&value,
		protected_storage, NULL) == CB_SUCCESS);
	callback_context.magic = 0;
	make_request();
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(executor_calls == 1U);
}

static void expect_dispatch_fail_stop(void)
{
	pid_t child = fork();
	int status;

	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_authority_dispatch();
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert_fail_stopped(status);
}

static void hostile_requests(void)
{
	reset_fixture();
	install();
	make_request();
	mailbox.reserved = 1;
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(!executor_calls);
	assert(!state_contains_capability());
	make_request();
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(!executor_calls);
	reset_fixture();
	install();
	make_request();
	make_request();
	mailbox.capability[0] ^= 1U;
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(mailbox.status ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SECURITY_VIOLATION);
	assert(mailbox.completion == PAYLOAD_MM_AUTHVAR_PRESENCE_COMPLETE);
	assert(!executor_calls);
	assert(!state_contains_capability());
	make_request();
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(!executor_calls);
	reset_fixture();
	install();
	make_request();
	dma_ok = false;
	expect_dispatch_fail_stop();
	reset_fixture();
	install();
	make_request();
	rendezvous_ok = false;
	expect_dispatch_fail_stop();
	reset_fixture();
	install();
	make_request();
	assert(payload_mm_authvar_presence_smi_dispatch(0xb3, 0xe8) == CB_ERR);
	assert(payload_mm_authvar_presence_smi_dispatch(0xb2, 0xe9) == CB_ERR);
	assert(!executor_calls);
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(executor_calls == 1U);
	assert(mailbox.status == PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_DEVICE_ERROR);
	assert(!state_contains_capability());
	make_request();
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(executor_calls == 1U);
	reset_fixture();
	install();
	make_request();
	corrupt_on_dma = true;
	expect_dispatch_fail_stop();
	reset_fixture();
	install();
	make_request();
	mutate_context_on_dma = true;
	expect_dispatch_fail_stop();
}

static void lifecycle_restrict(void)
{
	struct callback_context context_copy;
	pthread_t thread;

	reset_fixture();
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);

	reset_fixture();
	install();
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);
	assert(tail_generation(0) == 0 && tail_generation(1) == 0 &&
		tail_generation(2) == 7U && tail_generation(3) == 7U &&
		tail_generation(4) == 7U);
	set_tail_generation(0, 1U);
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);
	assert(!state_contains_capability());

	reset_fixture();
	install();
	assert(tail_generation(0) == 7U && tail_generation(1) == 7U);
	clear_install_gate();
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);
	assert(!state_contains_capability());

	reset_fixture();
	install();
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);
	clear_install_gate();
	{
		struct payload_mm_authvar_presence_policy value = policy();

		assert(payload_mm_authvar_presence_authority_install(&value,
			protected_storage, NULL) == CB_ERR);
	}
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);
	assert(!state_contains_capability());

	reset_fixture();
	install();
	memset(&mailbox_page, 0xa5, sizeof(mailbox_page));
	context_copy = callback_context;
	assert(state_contains_capability());
	assert(state_contains(&context_copy, sizeof(context_copy)));
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);
	assert(!state_contains_capability());
	assert(!state_contains(&context_copy, sizeof(context_copy)));
	assert(test_bytes_zero(&mailbox_page, sizeof(mailbox_page)));
	assert(!executor_calls);
	assert(payload_mm_authvar_presence_authority_restrict(8U) == CB_ERR);
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);

	reset_fixture();
	install();
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);
	{
		uint8_t *state;
		size_t state_size;

		state = (void *)(uintptr_t)
			payload_mm_authvar_presence_authority_test_state(&state_size);
		assert(state_size);
		state[0] ^= 1U;
	}
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);

	reset_fixture();
	install();
	memset(&mailbox_page, 0x5a, sizeof(mailbox_page));
	assert(payload_mm_authvar_presence_authority_restrict(0U) == CB_ERR);
	assert(!state_contains_capability());
	assert(test_bytes_zero(&mailbox_page, sizeof(mailbox_page)));
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);

	reset_fixture();
	install();
	assert(payload_mm_authvar_presence_authority_restrict(8U) == CB_ERR);
	assert(!state_contains_capability());
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);

	reset_fixture();
	install();
	corrupt_generation();
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);
	assert(!state_contains_capability());

	reset_fixture();
	install();
	payload_mm_authvar_presence_authority_restrict_test_hook(reenter_restrict);
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);
	assert(!state_contains_capability());
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);

	reset_fixture();
	install();
	restrict_hook_entered = false;
	restrict_hook_release = false;
	restrict_thread_result = CB_SUCCESS;
	payload_mm_authvar_presence_authority_restrict_test_hook(block_restrict);
	assert(!pthread_create(&thread, NULL, restrict_thread, NULL));
	assert(!pthread_mutex_lock(&restrict_mutex));
	while (!restrict_hook_entered)
		assert(!pthread_cond_wait(&restrict_cond, &restrict_mutex));
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);
	restrict_hook_release = true;
	assert(!pthread_cond_broadcast(&restrict_cond));
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(!pthread_join(thread, NULL));
	assert(restrict_thread_result == CB_SUCCESS);
	assert(!state_contains_capability());
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);

	reset_fixture();
	provision_zero = true;
	{
		struct payload_mm_authvar_presence_policy value = policy();

		assert(payload_mm_authvar_presence_authority_install(&value,
			protected_storage, NULL) == CB_ERR);
	}
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);

	reset_fixture();
	install();
	make_request();
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);

	reset_fixture();
	install();
	assert(state_contains_capability());
	payload_mm_authvar_presence_authority_close();
	assert(!state_contains_capability());
	make_request();
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(!executor_calls);
}

static void status_and_reset(void)
{
	static const struct {
		uint64_t executor;
		uint64_t wire;
	} cases[] = {
		{ PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED,
			PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_UNSUPPORTED },
		{ PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED,
			PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_WRITE_PROTECTED },
		{ PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED,
			PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ACCESS_DENIED },
		{ PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION,
			PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SECURITY_VIOLATION },
		{ PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES,
			PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_DEVICE_ERROR },
	};

	for (size_t index = 0; index < ARRAY_SIZE(cases); index++) {
		reset_fixture();
		install();
		make_request();
		executor_status = cases[index].executor;
		assert(payload_mm_authvar_presence_smi_dispatch(0xb2, 0xe8) == CB_ERR);
		assert(mailbox.status == cases[index].wire);
		assert(mailbox.completion == PAYLOAD_MM_AUTHVAR_PRESENCE_COMPLETE);
		assert(executor_calls == 1U && !reset_calls);
	}
	reset_fixture();
	install();
	make_request();
	executor_status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	executor_reset_required = true;
	assert(payload_mm_authvar_presence_smi_dispatch(0xb2, 0xe8) == CB_ERR);
	assert(executor_calls == 1U && reset_calls == 1U && reset_returned);
	assert(test_bytes_zero(&mailbox_page, sizeof(mailbox_page)));
	assert(payload_mm_authvar_presence_smi_dispatch(0xb2, 0xe8) == CB_ERR);
	assert(executor_calls == 1U && reset_calls == 1U);

	reset_fixture();
	install();
	make_request();
	executor_reset_required = true;
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(executor_calls == 1U && reset_calls == 1U);
	assert(test_bytes_zero(&mailbox_page, sizeof(mailbox_page)));
}

static void proof_transition_after_executor(void)
{
	pid_t child;
	int status;

	reset_fixture();
	install();
	make_request();
	revoke_dma_on_executor = true;
	child = fork();
	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_authority_dispatch();
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert_fail_stopped(status);

	reset_fixture();
	install();
	make_request();
	revoke_rendezvous_on_executor = true;
	executor_reset_required = true;
	child = fork();
	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_authority_dispatch();
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert_fail_stopped(status);

	reset_fixture();
	install();
	make_request();
	mutate_context_on_dma_call = 5U;
	executor_reset_required = true;
	child = fork();
	assert(child >= 0);
	if (!child) {
		(void)payload_mm_authvar_presence_authority_dispatch();
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert_fail_stopped(status);
}

static void dispatch_restrict_interleavings(void)
{
	pthread_t dispatch;
	pthread_t restrictor;

	/* A valid close requested while the executor owns dispatch is deferred. */
	reset_fixture();
	install();
	make_request();
	mailbox_page.bytes[80] = 0xa5U;
	mailbox_page.bytes[sizeof(mailbox_page) - 1U] = 0x5aU;
	executor_block = true;
	assert(!pthread_create(&dispatch, NULL, dispatch_thread, NULL));
	assert(!pthread_mutex_lock(&restrict_mutex));
	while (!executor_entered)
		assert(!pthread_cond_wait(&restrict_cond, &restrict_mutex));
	assert(mailbox_page.bytes[80] == 0xa5U);
	assert(mailbox_page.bytes[sizeof(mailbox_page) - 1U] == 0x5aU);
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);
	assert(mailbox_page.bytes[80] == 0xa5U);
	assert(mailbox_page.bytes[sizeof(mailbox_page) - 1U] == 0x5aU);
	assert(!pthread_mutex_lock(&restrict_mutex));
	executor_release = true;
	assert(!pthread_cond_broadcast(&restrict_cond));
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(!pthread_join(dispatch, NULL));
	assert(dispatch_thread_result == CB_ERR);
	assert(test_bytes_zero(&mailbox_page, sizeof(mailbox_page)));

	/* Dispatch loaded EXECUTING before a requester installed REQUESTED. */
	reset_fixture();
	install();
	make_request();
	restrict_hook_entered = false;
	restrict_hook_release = false;
	payload_mm_authvar_presence_authority_dispatch_finish_test_hook(
		block_dispatch_finish_once);
	assert(!pthread_create(&dispatch, NULL, dispatch_thread, NULL));
	assert(!pthread_mutex_lock(&restrict_mutex));
	while (!restrict_hook_entered)
		assert(!pthread_cond_wait(&restrict_cond, &restrict_mutex));
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);
	assert(!pthread_mutex_lock(&restrict_mutex));
	restrict_hook_release = true;
	assert(!pthread_cond_broadcast(&restrict_cond));
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(!pthread_join(dispatch, NULL));
	assert(test_bytes_zero(&mailbox_page, sizeof(mailbox_page)));

	/* Restrict loaded EXECUTING before dispatch published ATTEMPTED. */
	reset_fixture();
	install();
	make_request();
	restrict_hook_entered = false;
	restrict_hook_release = false;
	payload_mm_authvar_presence_authority_restrict_claim_test_hook(
		block_restrict_claim_once);
	assert(!pthread_create(&restrictor, NULL, restrict_thread, NULL));
	assert(!pthread_mutex_lock(&restrict_mutex));
	while (!restrict_hook_entered)
		assert(!pthread_cond_wait(&restrict_cond, &restrict_mutex));
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(!pthread_mutex_lock(&restrict_mutex));
	restrict_hook_release = true;
	assert(!pthread_cond_broadcast(&restrict_cond));
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(!pthread_join(restrictor, NULL));
	assert(restrict_thread_result == CB_SUCCESS);
	assert(test_bytes_zero(&mailbox_page, sizeof(mailbox_page)));

	/* Cleanup is complete before the terminal phase becomes observable. */
	reset_fixture();
	install();
	memset(&mailbox_page, 0x3c, sizeof(mailbox_page));
	payload_mm_authvar_presence_authority_cleanup_test_hook(observe_cleanup);
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);
	assert(cleanup_observed && cleanup_page_zero);

	/* Malformed/no-response close retries after an invalid poison request. */
	reset_fixture();
	install();
	make_request();
	mailbox.reserved = 1U;
	mailbox_page.bytes[80] = 0x7eU;
	restrict_hook_entered = false;
	restrict_hook_release = false;
	payload_mm_authvar_presence_authority_dispatch_finish_test_hook(
		block_dispatch_finish_once);
	assert(!pthread_create(&dispatch, NULL, dispatch_thread, NULL));
	assert(!pthread_mutex_lock(&restrict_mutex));
	while (!restrict_hook_entered)
		assert(!pthread_cond_wait(&restrict_cond, &restrict_mutex));
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(payload_mm_authvar_presence_authority_restrict(8U) == CB_ERR);
	assert(!pthread_mutex_lock(&restrict_mutex));
	restrict_hook_release = true;
	assert(!pthread_cond_broadcast(&restrict_cond));
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(!pthread_join(dispatch, NULL));
	assert(test_bytes_zero(&mailbox_page, sizeof(mailbox_page)));

	/* Reset-required close also retries and scrubs before reset returns. */
	reset_fixture();
	install();
	make_request();
	executor_reset_required = true;
	restrict_hook_entered = false;
	restrict_hook_release = false;
	payload_mm_authvar_presence_authority_dispatch_finish_test_hook(
		block_dispatch_finish_once);
	assert(!pthread_create(&dispatch, NULL, dispatch_thread, NULL));
	assert(!pthread_mutex_lock(&restrict_mutex));
	while (!restrict_hook_entered)
		assert(!pthread_cond_wait(&restrict_cond, &restrict_mutex));
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_ERR);
	assert(!pthread_mutex_lock(&restrict_mutex));
	restrict_hook_release = true;
	assert(!pthread_cond_broadcast(&restrict_cond));
	assert(!pthread_mutex_unlock(&restrict_mutex));
	assert(!pthread_join(dispatch, NULL));
	assert(reset_calls == 1U && test_bytes_zero(&mailbox_page,
		sizeof(mailbox_page)));
}

static void attempted_response_lifetime(void)
{
	reset_fixture();
	install();
	make_request();
	mailbox_page.bytes[80] = 0xa5U;
	mailbox_page.bytes[sizeof(mailbox_page) - 1U] = 0x5aU;
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(mailbox.completion == PAYLOAD_MM_AUTHVAR_PRESENCE_COMPLETE);
	assert(mailbox_page.bytes[80] == 0xa5U);
	assert(mailbox_page.bytes[sizeof(mailbox_page) - 1U] == 0x5aU);
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(mailbox_page.bytes[80] == 0xa5U);
	assert(mailbox_page.bytes[sizeof(mailbox_page) - 1U] == 0x5aU);
	payload_mm_authvar_presence_authority_cleanup_test_hook(observe_cleanup);
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);
	assert(cleanup_calls == 1U && cleanup_page_zero);
	mailbox_page.bytes[80] = 0x96U;
	mailbox_page.bytes[sizeof(mailbox_page) - 1U] = 0x69U;
	assert(payload_mm_authvar_presence_authority_restrict(7U) == CB_SUCCESS);
	assert(cleanup_calls == 1U);
	assert(mailbox_page.bytes[80] == 0x96U);
	assert(mailbox_page.bytes[sizeof(mailbox_page) - 1U] == 0x69U);
}

int main(void)
{
	install_validation();
	hostile_requests();
	lifecycle_restrict();
	status_and_reset();
	proof_transition_after_executor();
	dispatch_restrict_interleavings();
	attempted_response_lifetime();
	return 0;
}
