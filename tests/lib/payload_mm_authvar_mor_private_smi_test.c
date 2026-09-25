/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <boot/payload_mm_authvar_service.h>
#include <bootmem.h>
#include <cpu/x86/save_state.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "payload_mm_authvar_mor_private_smi_test_internal.h"

#define CHANNEL_TERMINAL 0xc3U

static uint8_t page[PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE]
	__aligned(PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE);
static struct payload_mm_authvar_mor_private_smi_slot slot;
static struct payload_mm_authvar_mor_seal_channel channel;
static const char *test_case;
static unsigned int control_calls;
static unsigned int protected_calls;
static unsigned int bootstrap_calls;
static bool mutate_during_verify;
static uintptr_t smi_identity[CONFIG_MAX_CPUS];
static uintptr_t smi_page_base[CONFIG_MAX_CPUS];
static uintptr_t smi_cookie[CONFIG_MAX_CPUS];
static uintptr_t smi_result[CONFIG_MAX_CPUS];
static pthread_barrier_t claim_barrier;
static pthread_barrier_t install_enter_barrier;
static pthread_barrier_t install_done_barrier;
static pthread_barrier_t abort_claim_barrier;
static pthread_barrier_t provision_enter_barrier;
static pthread_barrier_t provision_release_barrier;
static unsigned int terminal_cleanup_calls;
static unsigned int seed_calls;

static bool page_is_zero(void);

struct concurrent_receive {
	struct payload_mm_authvar_mor_private_smi_slot *slot;
	uint64_t identity;
	uint64_t cookie;
	enum cb_err result;
	uint64_t status;
};

struct concurrent_dispatch {
	unsigned int cpu;
	bool handled;
	uintptr_t result;
};

struct concurrent_provision {
	struct payload_mm_authvar_mor_private_smi_slot *slot;
	enum cb_err result;
};

static void *concurrent_provision_channel(void *argument)
{
	struct concurrent_provision *call = argument;

	call->result = payload_mm_authvar_mor_private_smi_loader_provision(
		call->slot, true);
	return NULL;
}

void payload_mm_authvar_mor_private_smi_test_during_provision(void)
{
	if (!strcmp(test_case, "provision-concurrent-terminal")) {
		int status = pthread_barrier_wait(&provision_enter_barrier);

		assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
		status = pthread_barrier_wait(&provision_release_barrier);
		assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
	}
}

static void *concurrent_bootstrap_receive(void *argument)
{
	struct concurrent_receive *call = argument;

	call->status = 99;
	call->result = payload_mm_authvar_mor_private_smi_test_bootstrap_receive(
		call->slot, call->identity, (uintptr_t)page, call->cookie, 0,
		&call->status);
	return NULL;
}

static void *concurrent_private_dispatch(void *argument)
{
	struct concurrent_dispatch *call = argument;

	if (!strcmp(test_case, "install-dispatch-barrier")) {
		int status = pthread_barrier_wait(&install_enter_barrier);

		assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
	}
	smi_result[call->cpu] = 99;
	call->handled = payload_mm_authvar_mor_private_smi_dispatch(call->cpu);
	call->result = smi_result[call->cpu];
	if (!strcmp(test_case, "install-dispatch-barrier")) {
		int status = pthread_barrier_wait(&install_done_barrier);

		assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
	}
	return NULL;
}

void payload_mm_authvar_mor_private_smi_test_before_claim(void)
{
	if (!strcmp(test_case, "installed-barrier") ||
	    !strcmp(test_case, "abort-dispatch-barrier")) {
		pthread_barrier_t *barrier = !strcmp(test_case, "installed-barrier") ?
			&claim_barrier : &abort_claim_barrier;
		const int status = pthread_barrier_wait(barrier);

		assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
	}
}

void payload_mm_authvar_mor_private_smi_test_during_install(void)
{
	if (!strcmp(test_case, "install-dispatch-barrier")) {
		int status = pthread_barrier_wait(&install_enter_barrier);

		assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
		status = pthread_barrier_wait(&install_done_barrier);
		assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
	}
}

void payload_mm_authvar_mor_private_smi_test_during_terminal(void)
{
	pthread_t thread;
	struct concurrent_dispatch dispatch = { .cpu = 1 };

	__atomic_add_fetch(&terminal_cleanup_calls, 1, __ATOMIC_RELAXED);
	if (strcmp(test_case, "terminal-concurrent"))
		return;
	assert(!pthread_create(&thread, NULL, concurrent_private_dispatch,
		&dispatch));
	assert(!pthread_join(thread, NULL));
	assert(dispatch.handled &&
		dispatch.result == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED);
}

void payload_mm_authvar_mor_private_smi_test_before_abort_claim(void)
{
	if (!strcmp(test_case, "abort-dispatch-barrier")) {
		const int status = pthread_barrier_wait(&abort_claim_barrier);

		assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
	}
}

void payload_mm_authvar_mor_private_smi_test_during_tombstone_publish(
	uint64_t identity)
{
	pthread_t thread;
	struct concurrent_dispatch dispatch = { .cpu = 1 };

	if (strcmp(test_case, "tombstone-publishing-dispatch"))
		return;
	for (unsigned int index = 0; index < CONFIG_MAX_CPUS; index++) {
		smi_identity[index] = identity;
		smi_page_base[index] = (uintptr_t)page;
		smi_cookie[index] = channel.caller_context;
	}
	assert(!pthread_create(&thread, NULL, concurrent_private_dispatch,
		&dispatch));
	assert(!pthread_join(thread, NULL));
	assert(!dispatch.handled);
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (result)
		return;
	abort();
}

bool platform_payload_mm_authvar_mor_private_smi_seed(
	struct payload_mm_authvar_mor_private_smi_seed *seed)
{
	seed_calls++;
	*seed = (struct payload_mm_authvar_mor_private_smi_seed) {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_REVISION,
		.size = sizeof(*seed),
		.cold_boot_generation = 7,
		.page_handle = { .opaque = { 1, 2 } },
	};
	memset(seed->capability, 0x5a, sizeof(seed->capability));
	memset(seed->receipt_secret, 0xa5, sizeof(seed->receipt_secret));
	return true;
}

int bootmem_aligned_reservation_query(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_aligned_reservation *reservation)
{
	if (handle->opaque[0] != 1 || handle->opaque[1] != 2)
		return -1;
	*reservation = (struct bootmem_aligned_reservation) {
		.base = (uintptr_t)page,
		.size = sizeof(page),
		.tag = BM_MEM_TABLE,
	};
	return 0;
}

enum cb_err bootmem_aligned_reservation_receipt_emit(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt *receipt)
{
	struct bootmem_reservation_receipt_authority claimed = { 0 };

	memset(receipt, 0, sizeof(*receipt));
	if (!bootmem_reservation_receipt_authority_claim(signer, &claimed))
		return CB_ERR;
	*receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(*receipt),
		.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		.generation = claimed.generation,
		.sequence = claimed.sequence,
		.handle = *handle,
		.base = (uintptr_t)page,
		.bytes = sizeof(page),
		.tag = BM_MEM_TABLE,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	assert(bootmem_reservation_receipt_mac(claimed.secret, receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt->mac) ==
		CB_SUCCESS);
	bootmem_reservation_receipt_close(&claimed);
	return CB_SUCCESS;
}

void bootmem_receipt_test_after_claim_cas(
	struct bootmem_reservation_receipt_authority *authority)
{
	if (mutate_during_verify && authority == &slot.verifier) {
		struct payload_mm_authvar_mor_private_smi_request *request =
			(void *)page;

		request->seal.grant.dma_policy_generation++;
	}
}

static bool protected(const void *base, size_t size)
{
	protected_calls++;
	if (!strncmp(test_case, "install-cut-", 12) &&
	    protected_calls == (unsigned int)(test_case[12] - '0'))
		return false;
	if (!strcmp(test_case, "install-mutate-slot") && protected_calls == 6)
		slot.maximum_cpus++;
	if (!strcmp(test_case, "install-mutate-channel") && protected_calls == 5)
		channel.caller++;
	return base && size;
}

static bool fixed_transport(const void *base, size_t size)
{
	return base == (const void *)(uintptr_t)channel.transport_base &&
		size == channel.transport_size;
}

static bool grant_protected(void *context, const void *base, size_t size)
{
	(void)context;
	return base && size;
}

enum cb_err platform_payload_mm_authvar_mor_private_smi_bootstrap(
	struct payload_mm_authvar_mor_private_smi_slot *protected_slot,
	const struct payload_mm_authvar_mor_seal_channel *seal_channel)
{
	uint64_t nested_status = 99;
	pthread_t thread;
	struct concurrent_receive concurrent = {
		.slot = protected_slot,
		.identity = seal_channel->caller,
		.cookie = seal_channel->caller_context,
	};
	struct concurrent_dispatch dispatch = { .cpu = 1 };

	bootstrap_calls++;
	if (!strcmp(test_case, "bootstrap-failure") ||
	    !strcmp(test_case, "bootstrap-failure-replay"))
		return CB_ERR;
	if (!strcmp(test_case, "bootstrap-reentry"))
		assert(payload_mm_authvar_mor_private_smi_test_bootstrap_receive(
			protected_slot, seal_channel->caller, (uintptr_t)page,
			seal_channel->caller_context, 0, &nested_status) == CB_ERR &&
			nested_status == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED);
	if (!strcmp(test_case, "bootstrap-concurrent")) {
		assert(!pthread_create(&thread, NULL, concurrent_bootstrap_receive,
			&concurrent));
		assert(!pthread_join(thread, NULL));
		assert(concurrent.result == CB_ERR &&
			concurrent.status == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED);
	}
	if (payload_mm_authvar_mor_private_smi_channel_install(protected_slot,
		seal_channel, protected) != CB_SUCCESS)
		return CB_ERR;
	if (payload_mm_authvar_mor_seal_channel_install(seal_channel,
		protected, fixed_transport) != CB_SUCCESS)
		return CB_ERR;
	if (!strcmp(test_case, "bootstrap-after-install-dispatch")) {
		assert(!pthread_create(&thread, NULL, concurrent_private_dispatch,
			&dispatch));
		assert(!pthread_join(thread, NULL));
		assert(dispatch.handled &&
			dispatch.result == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED);
	}
	if (!strcmp(test_case, "bootstrap-callback-header"))
		((struct payload_mm_authvar_mor_private_smi_request *)page)->
			cold_boot_generation++;
	if (!strcmp(test_case, "bootstrap-callback-padding"))
		page[sizeof(struct payload_mm_authvar_mor_private_smi_request)] = 1;
	return CB_SUCCESS;
}

uint64_t payload_mm_authvar_mor_control_clear_transaction(void)
{
	struct payload_mm_authvar_mor_grant grant = { 0 };
	pthread_t thread;
	struct concurrent_dispatch dispatch = { .cpu = 1 };

	control_calls++;
	if (!strcmp(test_case, "installed-concurrent")) {
		assert(!pthread_create(&thread, NULL, concurrent_private_dispatch,
			&dispatch));
		assert(!pthread_join(thread, NULL));
		assert(dispatch.handled &&
			dispatch.result == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED);
	}
	if (!strcmp(test_case, "transaction-before-take-failure"))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	assert(payload_mm_authvar_mor_grant_take(&grant, grant_protected,
		NULL, 0) == CB_SUCCESS);
	assert(payload_mm_authvar_mor_grant_validate(&grant) == CB_SUCCESS);
	memset(&grant, 0, sizeof(grant));
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

int get_save_state_reg(const enum cpu_reg reg, const int cpu, void *output,
	const uint8_t length)
{
	if (cpu < 0 || cpu >= CONFIG_MAX_CPUS || !output ||
	    length != sizeof(uintptr_t))
		return -1;
	switch (reg) {
	case RBX:
		*(uintptr_t *)output = smi_identity[cpu];
		return 0;
	case RSI:
		*(uintptr_t *)output = smi_page_base[cpu];
		return 0;
	case RDI:
		*(uintptr_t *)output = smi_cookie[cpu];
		return 0;
	default:
		return -1;
	}
}

int set_save_state_reg(const enum cpu_reg reg, const int cpu, void *input,
	const uint8_t length)
{
	if (cpu < 0 || cpu >= CONFIG_MAX_CPUS || reg != RAX || !input ||
	    length != sizeof(uintptr_t))
		return -1;
	smi_result[cpu] = *(uintptr_t *)input;
	return 0;
}

struct payload_mm_authvar_mor_private_smi_slot *
smm_get_payload_mm_authvar_mor_private_smi_slot(void)
{
	return &slot;
}

static enum cb_err trigger(
	const struct payload_mm_authvar_mor_private_smi_descriptor *descriptor,
	uint64_t *status, void *unused)
{
	struct payload_mm_authvar_mor_private_smi_request *request =
		(void *)page;
	uint64_t identity = descriptor->identity;
	uint64_t page_base = descriptor->page_base;
	uint64_t cookie = descriptor->cookie;
	unsigned int cpu = 0;
	pthread_t competing_thread;
	struct concurrent_dispatch competing = { .cpu = 1 };
	bool competing_started = false;

	(void)unused;
	if (!strcmp(test_case, "xapic-clobber") ||
	    !strcmp(test_case, "x2apic-clobber")) {
		/* APIC delivery owns RAX/RCX/RDX, never the descriptor registers. */
		volatile uint64_t rax = UINT64_MAX;
		volatile uint64_t rcx = UINT64_MAX;
		volatile uint64_t rdx = UINT64_MAX;

		rax = rcx = rdx = 0;
		assert(!rax && !rcx && !rdx);
		assert(identity == channel.caller);
		assert(page_base == (uintptr_t)page);
		assert(cookie == channel.caller_context);
	}
	if (!strcmp(test_case, "identity") ||
	    !strcmp(test_case, "bootstrap-identity"))
		identity ^= 1;
	else if (!strcmp(test_case, "page"))
		page_base += sizeof(page);
	else if (!strcmp(test_case, "cookie"))
		cookie ^= 1;
	else if (!strcmp(test_case, "cpu"))
		cpu = CONFIG_MAX_CPUS;
	else if (!strcmp(test_case, "other-cpu") ||
		 !strcmp(test_case, "bootstrap-other-cpu"))
		cpu = 1;
	else if (!strcmp(test_case, "non-owner-race")) {
		uint64_t first = 99;
		uint64_t second = 99;

		assert(payload_mm_authvar_mor_private_smi_test_receive(identity,
			page_base, cookie, 1, &first) == CB_ERR);
		assert(first == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED);
		assert(payload_mm_authvar_mor_private_smi_test_receive(identity,
			page_base, cookie, 0, &second) == CB_ERR);
		assert(second == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED);
		*status = first;
		return CB_SUCCESS;
	} else if (!strcmp(test_case, "policy-mismatch"))
		payload_mm_authvar_mor_private_smi_test_mutate_policy();
	else if (!strcmp(test_case, "tombstone-mismatch"))
		payload_mm_authvar_mor_private_smi_test_mutate_policy_identity();
	else if (!strcmp(test_case, "generation") ||
		 !strcmp(test_case, "bootstrap-generation"))
		request->cold_boot_generation++;
	else if (!strcmp(test_case, "capability") ||
		 !strcmp(test_case, "bootstrap-capability"))
		request->capability[0] ^= 1;
	else if (!strcmp(test_case, "receipt") ||
		 !strcmp(test_case, "bootstrap-receipt"))
		request->receipt.mac[0] ^= 1;
	else if (!strcmp(test_case, "padding") ||
		 !strcmp(test_case, "bootstrap-padding"))
		page[sizeof(*request)] = 1;
	else if (!strcmp(test_case, "seal"))
		request->seal.capability[0] ^= 1;
	else if (!strcmp(test_case, "callback-mutation"))
		mutate_during_verify = true;
	else if (!strcmp(test_case, "bootstrap-slot-cpus"))
		slot.maximum_cpus++;
	else if (!strcmp(test_case, "bootstrap-slot-cookie"))
		slot.descriptor_cookie = 1;
	else if (!strcmp(test_case, "bootstrap-slot-reserved"))
		slot.reserved[0] = 1;
	for (unsigned int index = 0; index < CONFIG_MAX_CPUS; index++) {
		smi_identity[index] = identity;
		smi_page_base[index] = page_base;
		smi_cookie[index] = cookie;
		smi_result[index] = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED;
	}
	if (!strcmp(test_case, "installed-barrier")) {
		assert(!pthread_create(&competing_thread, NULL,
			concurrent_private_dispatch, &competing));
		competing_started = true;
	}
	if (!payload_mm_authvar_mor_private_smi_dispatch(cpu))
		return CB_ERR;
	if (competing_started) {
		assert(!pthread_join(competing_thread, NULL));
		assert(competing.handled && competing.result ==
			PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED);
	}
	*status = smi_result[cpu];
	if (!strcmp(test_case, "bootstrap-generation") ||
	    !strcmp(test_case, "bootstrap-capability") ||
	    !strcmp(test_case, "bootstrap-padding") ||
	    !strcmp(test_case, "bootstrap-callback-header") ||
	    !strcmp(test_case, "bootstrap-callback-padding"))
		assert(page_is_zero());
	if (!strcmp(test_case, "bootstrap-receipt"))
		assert(!page_is_zero());
	return CB_SUCCESS;
}

static struct payload_mm_authvar_mor_grant valid_grant(void)
{
	struct payload_mm_authvar_mor_grant grant = {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REVISION,
		.size = sizeof(grant), .cold_boot_generation = 7,
		.entry = { .present = 1, .value = 1 },
		.flags = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS,
		.dma_policy_generation = 9, .inventory_generation = 12,
		.total_bytes = 0x3000, .cleared_bytes = 0x2000,
		.excluded_bytes = 0x1000, .total_spans = 2,
		.cleared_spans = 1, .excluded_spans = 1,
		.spans = {
			{ .base = 0x1000, .size = 0x2000,
			  .span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED },
			{ .base = 0x4000, .size = 0x1000,
			  .span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
			  .exclusion_reason = PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE },
		},
	};
	for (size_t index = 0; index < 32; index++) {
		grant.dma_policy_identity[index] = index + 1;
		grant.inventory_identity[index] = 0x80 + index;
	}
	return grant;
}

static bool page_is_zero(void)
{
	uint8_t combined = 0;

	for (size_t index = 0; index < sizeof(page); index++)
		combined |= page[index];
	return !combined;
}

static bool terminal_slot_is_clean(void)
{
	struct payload_mm_authvar_mor_private_smi_slot copy = slot;

	if (copy.state != CHANNEL_TERMINAL)
		return false;
	copy.state = 0;
	return !memcmp(&copy, &(struct payload_mm_authvar_mor_private_smi_slot){ 0 },
		sizeof(copy));
}

int main(int argc, char **argv)
{
	struct payload_mm_authvar_mor_grant grant = valid_grant();
	uint64_t replay_status = 99;
	bool close;
	bool successful;
	bool bootstrap;
	pthread_t install_dispatch_thread;
	struct concurrent_dispatch install_dispatch = { .cpu = 1 };

	assert(argc == 2);
	test_case = argv[1];
	if (!strcmp(test_case, "installed-barrier"))
		assert(!pthread_barrier_init(&claim_barrier, NULL, 2));
	if (!strcmp(test_case, "install-dispatch-barrier")) {
		assert(!pthread_barrier_init(&install_enter_barrier, NULL, 2));
		assert(!pthread_barrier_init(&install_done_barrier, NULL, 2));
	}
	if (!strcmp(test_case, "abort-dispatch-barrier")) {
		assert(!pthread_barrier_init(&abort_claim_barrier, NULL, 2));
	}
	if (!strcmp(test_case, "provision-concurrent-terminal")) {
		pthread_t thread;
		struct concurrent_provision call = { .slot = &slot };
		int status;

		assert(!pthread_barrier_init(&provision_enter_barrier, NULL, 2));
		assert(!pthread_barrier_init(&provision_release_barrier, NULL, 2));
		assert(!pthread_create(&thread, NULL, concurrent_provision_channel,
			&call));
		status = pthread_barrier_wait(&provision_enter_barrier);
		assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
		assert(payload_mm_authvar_mor_private_smi_close_unused() == CB_ERR);
		status = pthread_barrier_wait(&provision_release_barrier);
		assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
		assert(!pthread_join(thread, NULL));
		assert(call.result == CB_SUCCESS && seed_calls == 1);
		assert(payload_mm_authvar_mor_private_smi_seal_channel_resolve(
			&channel) == CB_SUCCESS);
		assert(!pthread_barrier_destroy(&provision_enter_barrier));
		assert(!pthread_barrier_destroy(&provision_release_barrier));
		return 0;
	}
	bootstrap = !strncmp(test_case, "bootstrap-", 10);
	close = !strcmp(test_case, "close") ||
		!strcmp(test_case, "bootstrap-close");
	successful = !strcmp(test_case, "success") ||
		!strcmp(test_case, "replay") ||
		!strcmp(test_case, "xapic-clobber") ||
		!strcmp(test_case, "x2apic-clobber") ||
		!strcmp(test_case, "install-dispatch-barrier") ||
		!strcmp(test_case, "tombstone-publishing-dispatch") ||
		!strcmp(test_case, "installed-concurrent") ||
		!strcmp(test_case, "terminal-concurrent") ||
		!strcmp(test_case, "bootstrap-success") ||
		!strcmp(test_case, "bootstrap-replay") ||
		!strcmp(test_case, "bootstrap-after-install-dispatch");
	if (!strcmp(test_case, "optional")) {
		memset(&slot, 0xa5, sizeof(slot));
		assert(payload_mm_authvar_mor_private_smi_loader_provision(&slot,
			false) == CB_SUCCESS);
		assert(!seed_calls);
		assert(!memcmp(&slot,
			&(struct payload_mm_authvar_mor_private_smi_slot){ 0 },
			sizeof(slot)));
		return 0;
	}
	assert(payload_mm_authvar_mor_private_smi_loader_provision(&slot, true) ==
		CB_SUCCESS);
	assert(seed_calls == 1);
	assert(payload_mm_authvar_mor_private_smi_seal_channel_resolve(&channel) ==
		CB_SUCCESS);
	if (!strncmp(test_case, "install-mutate-", 15) ||
	    !strncmp(test_case, "install-cut-", 12)) {
		assert(payload_mm_authvar_mor_private_smi_channel_install(&slot,
			&channel, protected) == CB_ERR);
		assert(page_is_zero());
		assert(terminal_slot_is_clean());
		return 0;
	}
	if (!bootstrap) {
		if (!strcmp(test_case, "install-dispatch-barrier")) {
			for (unsigned int index = 0; index < CONFIG_MAX_CPUS; index++) {
				smi_identity[index] = channel.caller;
				smi_page_base[index] = (uintptr_t)page;
				smi_cookie[index] = channel.caller_context;
			}
			assert(!pthread_create(&install_dispatch_thread, NULL,
				concurrent_private_dispatch, &install_dispatch));
		}
		assert(payload_mm_authvar_mor_private_smi_channel_install(&slot,
			&channel, protected) == CB_SUCCESS);
		if (!strcmp(test_case, "install-dispatch-barrier")) {
			assert(!pthread_join(install_dispatch_thread, NULL));
			assert(!install_dispatch.handled);
		}
		assert(payload_mm_authvar_mor_seal_channel_install(&channel,
			protected, fixed_transport) == CB_SUCCESS);
		if (!strcmp(test_case, "abort-dispatch-barrier")) {
			pthread_t thread;
			struct concurrent_dispatch dispatch = { .cpu = 1 };

			for (unsigned int index = 0; index < CONFIG_MAX_CPUS; index++) {
				smi_identity[index] = channel.caller;
				smi_page_base[index] = (uintptr_t)page;
				smi_cookie[index] = channel.caller_context;
			}
			assert(!pthread_create(&thread, NULL,
				concurrent_private_dispatch, &dispatch));
			payload_mm_authvar_mor_private_smi_channel_abort();
			assert(!pthread_join(thread, NULL));
			assert(dispatch.handled);
			assert(__atomic_load_n(&terminal_cleanup_calls,
				__ATOMIC_RELAXED) == 1);
			assert(page_is_zero() && terminal_slot_is_clean());
			assert(!pthread_barrier_destroy(&abort_claim_barrier));
			return 0;
		}
	}
	payload_mm_authvar_mor_private_smi_test_set_trigger(trigger, NULL);
	if (close) {
		assert(payload_mm_authvar_mor_private_smi_close_unused() ==
			CB_SUCCESS);
	} else if (!strcmp(test_case, "installed-barrier")) {
		const enum cb_err status =
			payload_mm_authvar_mor_private_smi_send_install(&grant);

		assert(status == CB_SUCCESS || status == CB_ERR);
	} else if (successful) {
		assert(payload_mm_authvar_mor_private_smi_send_install(&grant) ==
			CB_SUCCESS);
	} else {
		assert(payload_mm_authvar_mor_private_smi_send_install(&grant) ==
			CB_ERR);
	}
	assert(page_is_zero());
	assert(bootstrap_calls == (bootstrap && !close &&
		strcmp(test_case, "bootstrap-identity") &&
		strcmp(test_case, "bootstrap-generation") &&
		strcmp(test_case, "bootstrap-capability") &&
		strcmp(test_case, "bootstrap-receipt") &&
		strcmp(test_case, "bootstrap-padding") &&
		strcmp(test_case, "bootstrap-other-cpu") &&
		strcmp(test_case, "bootstrap-slot-cpus") &&
		strcmp(test_case, "bootstrap-slot-cookie") &&
		strcmp(test_case, "bootstrap-slot-reserved")));
	if (bootstrap && strcmp(test_case, "bootstrap-identity"))
		assert(terminal_slot_is_clean());
	if (!strcmp(test_case, "transaction-before-take-failure")) {
		struct payload_mm_authvar_mor_grant output = { 0 };

		assert(!payload_mm_authvar_mor_grant_ready());
		assert(payload_mm_authvar_mor_grant_take(&output,
			grant_protected, NULL, 0) == CB_ERR);
		assert(!memcmp(&output,
			&(struct payload_mm_authvar_mor_grant){ 0 }, sizeof(output)));
	}
	if (!strcmp(test_case, "installed-barrier")) {
		assert(control_calls <= 1);
	} else {
		assert(control_calls == (close ? 0U :
			(successful ||
			 !strcmp(test_case, "transaction-before-take-failure"))));
	}
	if (!strcmp(test_case, "replay") ||
	    !strcmp(test_case, "tombstone-mismatch") ||
	    !strcmp(test_case, "bootstrap-replay") ||
	    !strcmp(test_case, "bootstrap-failure-replay")) {
		smi_identity[1] = channel.caller;
		smi_page_base[1] = (uintptr_t)page;
		smi_cookie[1] = channel.caller_context;
		smi_result[1] = 99;
		assert(payload_mm_authvar_mor_private_smi_dispatch(1));
		replay_status = smi_result[1];
		assert(replay_status == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED);
	}
	if (!strcmp(test_case, "installed-barrier"))
		assert(!pthread_barrier_destroy(&claim_barrier));
	if (!strcmp(test_case, "install-dispatch-barrier")) {
		assert(!pthread_barrier_destroy(&install_enter_barrier));
		assert(!pthread_barrier_destroy(&install_done_barrier));
	}
	return 0;
}
