/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <boot/payload_mm_authvar_service.h>
#include <bootmem.h>
#include <cpu/x86/save_state.h>
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
static bool mutate_during_verify;

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

uint64_t payload_mm_authvar_mor_control_clear_transaction(void)
{
	struct payload_mm_authvar_mor_grant grant = { 0 };

	control_calls++;
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
	(void)reg;
	(void)cpu;
	(void)output;
	(void)length;
	return -1;
}

int set_save_state_reg(const enum cpu_reg reg, const int cpu, void *input,
	const uint8_t length)
{
	(void)reg;
	(void)cpu;
	(void)input;
	(void)length;
	return -1;
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
	if (!strcmp(test_case, "identity"))
		identity ^= 1;
	else if (!strcmp(test_case, "page"))
		page_base += sizeof(page);
	else if (!strcmp(test_case, "cookie"))
		cookie ^= 1;
	else if (!strcmp(test_case, "cpu"))
		cpu = CONFIG_MAX_CPUS;
	else if (!strcmp(test_case, "other-cpu"))
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
	else if (!strcmp(test_case, "generation"))
		request->cold_boot_generation++;
	else if (!strcmp(test_case, "capability"))
		request->capability[0] ^= 1;
	else if (!strcmp(test_case, "receipt"))
		request->receipt.mac[0] ^= 1;
	else if (!strcmp(test_case, "padding"))
		page[sizeof(*request)] = 1;
	else if (!strcmp(test_case, "seal"))
		request->seal.capability[0] ^= 1;
	else if (!strcmp(test_case, "callback-mutation"))
		mutate_during_verify = true;
	return payload_mm_authvar_mor_private_smi_test_receive(identity, page_base,
		cookie, cpu, status);
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

	assert(argc == 2);
	test_case = argv[1];
	close = !strcmp(test_case, "close");
	successful = !strcmp(test_case, "success") ||
		!strcmp(test_case, "replay") ||
		!strcmp(test_case, "xapic-clobber") ||
		!strcmp(test_case, "x2apic-clobber");
	assert(payload_mm_authvar_mor_private_smi_loader_provision(&slot) ==
		CB_SUCCESS);
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
	assert(payload_mm_authvar_mor_private_smi_channel_install(&slot, &channel,
		protected) == CB_SUCCESS);
	assert(payload_mm_authvar_mor_seal_channel_install(&channel, protected,
		fixed_transport) == CB_SUCCESS);
	payload_mm_authvar_mor_private_smi_test_set_trigger(trigger, NULL);
	if (close) {
		payload_mm_authvar_mor_private_smi_close_unused();
	} else if (successful) {
		assert(payload_mm_authvar_mor_private_smi_send_install(&grant) ==
			CB_SUCCESS);
	} else {
		assert(payload_mm_authvar_mor_private_smi_send_install(&grant) ==
			CB_ERR);
	}
	assert(page_is_zero());
	if (!strcmp(test_case, "transaction-before-take-failure")) {
		struct payload_mm_authvar_mor_grant output = { 0 };

		assert(!payload_mm_authvar_mor_grant_ready());
		assert(payload_mm_authvar_mor_grant_take(&output,
			grant_protected, NULL, 0) == CB_ERR);
		assert(!memcmp(&output,
			&(struct payload_mm_authvar_mor_grant){ 0 }, sizeof(output)));
	}
	assert(control_calls == (close ? 0U :
		(successful ||
		 !strcmp(test_case, "transaction-before-take-failure"))));
	if (!strcmp(test_case, "replay"))
		assert(payload_mm_authvar_mor_private_smi_test_receive(
			channel.caller, (uintptr_t)page, channel.caller_context,
			1, &replay_status) == CB_ERR &&
			replay_status == PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED);
	return 0;
}
