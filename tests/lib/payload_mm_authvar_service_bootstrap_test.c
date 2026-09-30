/* SPDX-License-Identifier: GPL-2.0-only */

/* Keep the existing real bootstrap/media fixture, not a copied owner. */
#define main legacy_bootstrap_fixture_main
#include "payload_mm_authvar_smm_bootstrap_test.c"
#undef main
#undef receipt

#include <boot/payload_mm_authvar_presence_bootstrap.h>
#include <boot/payload_mm_authvar_service_receiver.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include "../../src/lib/bootmem_reservation_receipt_internal.h"

struct smm_invocation_runtime_view { unsigned int checked; };
static const struct smm_invocation_runtime_view runtime_view = { 1 };
static struct payload_mm_authvar_presence_transaction_binding canonical;
static struct bootmem_reservation_receipt_authority service_verifier;
static struct bootmem_reservation_receipt service_receipt;
static bool identity_available = true;
static bool drift_during_consume;
static bool abort_during_consume;
static unsigned int binding_reads;
static bool wave_admitted = true;

/* This prerequisite fixture installs only the backend boundary model. */
__weak enum cb_err payload_mm_authvar_executor_service_admit(
	const struct lb_authvar_service_endpoint *endpoint,
	const void *request, void *response, size_t size)
{
	(void)endpoint;
	(void)request;
	(void)response;
	(void)size;
	return CB_ERR;
}

__weak enum cb_err payload_mm_authvar_service_transaction(
	const struct lb_authvar_service_endpoint *endpoint,
	const void *request, void *response, size_t size)
{
	(void)endpoint;
	(void)request;
	(void)response;
	(void)size;
	return CB_ERR;
}

__weak void __noreturn smm_invocation_platform_fail_stop(void)
{
	abort();
}

bool platform_payload_mm_authvar_service_bootstrap_admitted(void)
{
	return wave_admitted;
}

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **view)
{
	*view = &runtime_view;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base, size_t size)
{
	assert(view == &runtime_view);
	return base && size && (uintptr_t)base >= 0x400000U &&
		(uintptr_t)base < 0x400000U + runtime_smram_size &&
		size <= 0x400000U + runtime_smram_size - (uintptr_t)base ?
		CB_SUCCESS : CB_ERR;
}

/* This is the separately tested genuine loader identity/topology boundary. */
enum cb_err payload_mm_authvar_presence_bootstrap_binding_get(
	const struct payload_mm_authvar_presence_transaction_binding **binding)
{
	binding_reads++;
	*binding = identity_available ? &canonical : NULL;
	return identity_available ? CB_SUCCESS : CB_ERR;
}

void bootmem_receipt_test_after_claim_cas(
	struct bootmem_reservation_receipt_authority *authority)
{
	assert(authority == &service_verifier);
	if (drift_during_consume)
		canonical.capability[0]++;
	if (abort_during_consume)
		payload_mm_authvar_service_prepare_abort();
}

static void make_service_receipt(void)
{
	struct bootmem_reservation_receipt_authority signer = { 0 };
	struct bootmem_aligned_reservation_handle handle = { .opaque = { 3, 7 } };
	uint8_t key[32];
	uint8_t secret[32];

	memset(&service_verifier, 0, sizeof(service_verifier));
	for (size_t index = 0; index < sizeof(key); index++)
		key[index] = (uint8_t)index;
	memcpy(secret, key, sizeof(secret));
	assert(bootmem_reservation_receipt_provision(&signer, &service_verifier,
		secret, 1, 9, &handle) == CB_SUCCESS);
	/* Independent public layout offsets, not an implementation-shaped clone. */
	uint8_t raw[96] __aligned(8) = { 0 };
	uint32_t value32 = 1;
	uint64_t value64 = 9;

	memcpy(raw, &value32, 4);
	value32 = 96;
	memcpy(raw + 4, &value32, 4);
	value32 = 1;
	memcpy(raw + 8, &value32, 4);
	memcpy(raw + 16, &value64, 8);
	value64 = 1;
	memcpy(raw + 24, &value64, 8);
	value32 = 3;
	memcpy(raw + 32, &value32, 4);
	value32 = 7;
	memcpy(raw + 36, &value32, 4);
	value64 = 0x100000;
	memcpy(raw + 40, &value64, 8);
	value64 = 65536;
	memcpy(raw + 48, &value64, 8);
	value32 = 0x1000b;
	memcpy(raw + 56, &value32, 4);
	value32 = 1;
	memcpy(raw + 60, &value32, 4);
	assert(bootmem_reservation_receipt_mac(key, raw, 64, raw + 64) == CB_SUCCESS);
	memcpy(&service_receipt, raw, sizeof(raw));
	bootmem_reservation_receipt_close(&signer);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	initialize();
	canonical.generation = 9;
	canonical.capability[0] = 1;
	make_service_receipt();
	if (!strcmp(argv[1], "absent-identity"))
		identity_available = false;
	else if (!strcmp(argv[1], "mac"))
		service_receipt.mac[0]++;
	else if (!strcmp(argv[1], "tag"))
		service_receipt.tag = BM_MEM_RESERVED;
	else if (!strcmp(argv[1], "size"))
		service_receipt.bytes--;
	else if (!strcmp(argv[1], "generation"))
		service_receipt.generation++;
	else if (!strcmp(argv[1], "unaligned"))
		service_receipt.base++;
	else if (!strcmp(argv[1], "high"))
		service_receipt.base = 0xffff1000ULL;
	else if (!strcmp(argv[1], "smram"))
		service_receipt.base = 0x400000U;
	else if (!strcmp(argv[1], "drift"))
		drift_during_consume = true;
	else if (!strcmp(argv[1], "abort-active"))
		abort_during_consume = true;
	else if (!strcmp(argv[1], "abort-empty"))
		payload_mm_authvar_service_prepare_abort();
	else if (!strcmp(argv[1], "absent-wave"))
		wave_admitted = false;
	else if (!strcmp(argv[1], "alias")) {
		struct bootmem_reservation_receipt saved = service_receipt;

		assert(payload_mm_authvar_service_prepare(
			(void *)&service_receipt, &service_receipt) == CB_ERR);
		assert(!memcmp(&saved, &service_receipt, sizeof(saved)));
		assert(payload_mm_authvar_service_prepare(&service_verifier,
			&service_receipt) == CB_SUCCESS);
		return 0;
	}
	/* Geometry/tag cases carry genuine MACs, so they test admission, not tamper. */
	if (!strcmp(argv[1], "tag") || !strcmp(argv[1], "size") ||
	    !strcmp(argv[1], "generation") || !strcmp(argv[1], "unaligned") ||
	    !strcmp(argv[1], "high") || !strcmp(argv[1], "smram")) {
		uint8_t key[32];

		for (size_t index = 0; index < sizeof(key); index++)
			key[index] = (uint8_t)index;
		assert(bootmem_reservation_receipt_mac(key, &service_receipt, 64,
			service_receipt.mac) == CB_SUCCESS);
	}
	bool success = !strcmp(argv[1], "success") ||
		!strcmp(argv[1], "with-mor") || !strcmp(argv[1], "repeat") ||
		!strcmp(argv[1], "abort-prepared") || !strcmp(argv[1], "spi-denied") ||
		!strcmp(argv[1], "absent-wave");

	assert(payload_mm_authvar_service_prepare(&service_verifier, &service_receipt) ==
		(success ? CB_SUCCESS : CB_ERR));
	if (!success) {
		assert(payload_mm_authvar_smm_service_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
		return 0;
	}
	assert(!memcmp(&service_receipt, &(struct bootmem_reservation_receipt) { 0 },
		sizeof(service_receipt)));
	assert(binding_reads >= 2);
	if (!strcmp(argv[1], "absent-wave")) {
		assert(payload_mm_authvar_smm_service_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
		return 0;
	}
	if (!strcmp(argv[1], "repeat")) {
		make_service_receipt();
		assert(payload_mm_authvar_service_prepare(&service_verifier,
			&service_receipt) == CB_ERR);
	}
	if (!strcmp(argv[1], "abort-prepared")) {
		payload_mm_authvar_service_prepare_abort();
		payload_mm_authvar_service_prepare_abort();
		assert(payload_mm_authvar_smm_service_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
		return 0;
	}
	if (strcmp(argv[1], "with-mor"))
		memset(&seed.seal_channel, 0, sizeof(seed.seal_channel));
	if (!strcmp(argv[1], "spi-denied")) {
		restricted = false;
		assert(payload_mm_authvar_smm_service_bootstrap_install(&seed) == CB_ERR);
		assert(step == 1);
		return 0;
	}
	assert(payload_mm_authvar_smm_service_bootstrap_install(&seed) == CB_SUCCESS);
	assert(step == (!strcmp(argv[1], "with-mor") ? 5U : 4U));
	assert(installed_contract.communication.base == 0x100000U &&
		installed_contract.communication.size == 65536U);
	assert(payload_mm_authvar_smm_service_bootstrap_install(&seed) == CB_ERR);
	return 0;
}
