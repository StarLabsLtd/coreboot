/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_service.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static struct payload_mm_authvar_presence_message mailbox __aligned(8);
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

struct callback_context {
	uint32_t magic;
};

static struct callback_context callback_context;

static bool state_contains_capability(void);

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
	const uintptr_t mailbox_start = (uintptr_t)&mailbox;

	(void)context;
	if (mailbox_is_protected)
		return storage != NULL && size != 0U;
	return storage != NULL && size != 0U &&
		(start > mailbox_start || mailbox_start - start >= size) &&
		(mailbox_start > start || start - mailbox_start >= sizeof(mailbox));
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
	return dma_ok && base == (uintptr_t)&mailbox && size == sizeof(mailbox);
}

static bool rendezvous(void *context)
{
	struct callback_context *value = context;

	assert(value && value->magic == 0x13579bdfU);
	return rendezvous_ok;
}

static void cold_reset(void *context)
{
	struct callback_context *value = context;

	assert(value && value->magic == 0x13579bdfU);
	reset_calls++;
	reset_returned = true;
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
			.communication_base = (uintptr_t)&mailbox,
			.communication_size = sizeof(mailbox),
			.message_size = sizeof(mailbox),
			.transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
			.trigger_width = 1,
			.trigger_address = 0xb2,
			.trigger_value = 0xe8,
			.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
			.capability_size = LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
		},
		.provision = provision,
		.dma_protected = dma_protected,
		.cpu_rendezvous_active = rendezvous,
		.cold_reset = cold_reset,
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
	memset(&mailbox, 0, sizeof(mailbox));
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

static void install_validation(void)
{
	struct payload_mm_authvar_presence_policy value;

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
	reset_fixture();
	provision_zero = true;
	value = policy();
	assert(payload_mm_authvar_presence_authority_install(&value,
		protected_storage, NULL) == CB_ERR);
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
	assert(payload_mm_authvar_presence_authority_install(&value,
		protected_storage, NULL) == CB_ERR);
	reset_fixture();
	value = policy();
	assert(payload_mm_authvar_presence_authority_install(&value,
		protected_storage, NULL) == CB_SUCCESS);
	callback_context.magic = 0;
	make_request();
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(executor_calls == 1U);
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
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(!executor_calls);
	assert(!state_contains_capability());
	reset_fixture();
	install();
	make_request();
	rendezvous_ok = false;
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(!executor_calls);
	assert(!state_contains_capability());
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
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(!executor_calls);
	assert(!state_contains_capability());
	reset_fixture();
	install();
	make_request();
	mutate_context_on_dma = true;
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(!executor_calls);
	assert(!state_contains_capability());
}

static void lifecycle_close(void)
{
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
	assert(mailbox.status == PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SUCCESS);
	assert(mailbox.completion == PAYLOAD_MM_AUTHVAR_PRESENCE_COMPLETE);
	assert(payload_mm_authvar_presence_smi_dispatch(0xb2, 0xe8) == CB_ERR);
	assert(executor_calls == 1U && reset_calls == 1U);

	reset_fixture();
	install();
	make_request();
	executor_reset_required = true;
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(executor_calls == 1U && reset_calls == 1U);
	assert(mailbox.status == PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_DEVICE_ERROR);
	assert(mailbox.completion == PAYLOAD_MM_AUTHVAR_PRESENCE_COMPLETE);
}

static void proof_transition_after_executor(void)
{
	reset_fixture();
	install();
	make_request();
	revoke_dma_on_executor = true;
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(executor_calls == 1U && !reset_calls);
	assert(mailbox.status == PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_PENDING &&
		mailbox.completion == PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING);

	reset_fixture();
	install();
	make_request();
	revoke_rendezvous_on_executor = true;
	executor_reset_required = true;
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(executor_calls == 1U && reset_calls == 1U);
	assert(mailbox.status == PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_PENDING &&
		mailbox.completion == PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING);

	reset_fixture();
	install();
	make_request();
	mutate_context_on_dma_call = 3U;
	executor_reset_required = true;
	assert(payload_mm_authvar_presence_authority_dispatch() == CB_ERR);
	assert(executor_calls == 1U && reset_calls == 1U);
	assert(mailbox.status == PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_PENDING &&
		mailbox.completion == PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING);
}

int main(void)
{
	install_validation();
	hostile_requests();
	lifecycle_close();
	status_and_reset();
	proof_transition_after_executor();
	return 0;
}
