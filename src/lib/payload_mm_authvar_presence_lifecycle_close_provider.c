/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_authvar_presence_lifecycle_close_endpoint_internal.h"
#include "payload_mm_authvar_presence_producer_internal.h"
#include "payload_mm_authvar_presence_lifecycle_close_provider_internal.h"
#include <boot/payload_mm_authvar_presence_lifecycle_close_transport.h>
#include <cpu/x86/apm.h>
#include <cpu/x86/smm_command.h>
#include <halt.h>
#include <random.h>
#include <string.h>

#if !ENV_RAMSTAGE && !ENV_TEST
#error "Authenticated-variable presence lifecycle-close provider is ramstage-only"
#endif

enum provider_state {
	PROVIDER_EMPTY,
	PROVIDER_INSTALLING,
	PROVIDER_READY,
	PROVIDER_DELIVERED,
	PROVIDER_COMMITTED,
	PROVIDER_FAILED,
};

struct provider_owner {
	uint32_t state;
	uint32_t reserved;
	struct payload_mm_authvar_presence_lifecycle_close_backing backing;
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt ready;
};

static struct provider_owner provider;

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

__weak enum cb_err
platform_payload_mm_authvar_presence_lifecycle_close_route_install(
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		*descriptor,
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt *receipt)
{
	(void)descriptor;
	if (receipt)
		scrub(receipt, sizeof(*receipt));
	return CB_ERR;
}

__weak void __noreturn
platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop(void)
{
#if ENV_TEST
	__builtin_trap();
#else
	die("Authenticated-variable presence lifecycle-close route failure\n");
#endif
}

static bool receipt_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_install_receipt
		*receipt,
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		*descriptor)
{
	return receipt && descriptor &&
		!memcmp(&receipt->descriptor, descriptor, sizeof(*descriptor)) &&
		receipt->protected_route_identity && receipt->route_nonce &&
		receipt->installed == 1U && !receipt->reserved;
}

static void provider_fail(void)
{
	if (provider.backing.base &&
	    payload_mm_authvar_presence_lifecycle_close_backing_cancel(
		&provider.backing) != CB_SUCCESS)
		platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
	else if (!provider.backing.base)
		payload_mm_authvar_presence_lifecycle_close_backing_abort();
	scrub((uint8_t *)&provider + offsetof(struct provider_owner, backing),
		sizeof(provider) - offsetof(struct provider_owner, backing));
	__atomic_store_n(&provider.state, PROVIDER_FAILED, __ATOMIC_RELEASE);
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_provider_prepare(
	const struct payload_mm_authvar_presence_receipt *presence_receipt)
{
	struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		descriptor = { 0 };
	struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		frozen = { 0 };
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt
		installed = { 0 };
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt ready = { 0 };
	struct lb_authvar_presence_endpoint presence;
	struct payload_mm_authvar_presence_lifecycle_close_backing backing_snapshot;
	enum cb_err install_status;
	uint32_t expected = PROVIDER_EMPTY;
	bool route_live = false;
	enum cb_err status = CB_ERR;

	if (!CONFIG(SMM_APMC_ROUTE_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE) ||
	    !presence_receipt ||
	    !__atomic_compare_exchange_n(&provider.state, &expected,
		PROVIDER_INSTALLING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (!payload_mm_authvar_presence_producer_publication_receipt_validate(
		presence_receipt))
		goto out;
	presence = presence_receipt->endpoint;
	if (!presence.generation ||
	    payload_mm_authvar_presence_lifecycle_close_backing_take(
		&provider.backing) != CB_SUCCESS ||
	    !payload_mm_authvar_presence_producer_publication_receipt_validate(
		presence_receipt) ||
	    memcmp(&presence, &presence_receipt->endpoint, sizeof(presence)))
		goto out;
	descriptor =
		(struct payload_mm_authvar_presence_lifecycle_close_install_descriptor) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_REVISION,
			.size = sizeof(descriptor),
			.generation = presence.generation,
			.backing_base = provider.backing.base,
			.backing_size = provider.backing.bytes,
			.message_size =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
			.trigger_address = APM_CNT,
			.trigger_value =
				SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
			.source_mask =
				LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK,
		};
	for (size_t index = 0; index < ARRAY_SIZE(descriptor.challenge); index++)
		if (get_random_number_64(&descriptor.challenge[index]) != CB_SUCCESS ||
		    !descriptor.challenge[index])
			goto out;
	frozen = descriptor;
	backing_snapshot = provider.backing;
	if (!payload_mm_authvar_presence_producer_publication_receipt_validate(
		presence_receipt))
		goto out;
	install_status =
		platform_payload_mm_authvar_presence_lifecycle_close_route_install(
			&descriptor, &installed);
	if (install_status != CB_SUCCESS) {
		if (memcmp(&descriptor, &frozen, sizeof(frozen)) ||
		    memcmp(&provider.backing, &backing_snapshot,
			sizeof(backing_snapshot)) ||
		    !zero(&installed, sizeof(installed)) ||
		    __atomic_load_n(&provider.state, __ATOMIC_ACQUIRE) !=
			PROVIDER_INSTALLING)
			platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
		goto out;
	}
	route_live = true;
	if (memcmp(&descriptor, &frozen, sizeof(frozen)) ||
	    !payload_mm_authvar_presence_producer_publication_receipt_validate(
		presence_receipt) ||
	    memcmp(&presence, &presence_receipt->endpoint, sizeof(presence)) ||
	    !receipt_valid(&installed, &frozen))
		goto out;
	ready = (struct payload_mm_authvar_presence_lifecycle_close_ready_receipt) {
		.revision =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_READY_RECEIPT_REVISION,
		.size = sizeof(ready),
		.endpoint = {
			.tag = LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT,
			.size = sizeof(ready.endpoint),
			.revision =
				LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_REVISION,
			.header_size = sizeof(ready.endpoint),
			.flags =
				LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_REQUIRED_FLAGS,
			.generation = frozen.generation,
			.communication_base = frozen.backing_base,
			.communication_size =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
			.message_size =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
			.transport =
				LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT_APM_IO8,
			.trigger_width = sizeof(uint8_t),
			.trigger_address = frozen.trigger_address,
			.trigger_value = frozen.trigger_value,
			.source_mask = frozen.source_mask,
		},
		.identity = installed.protected_route_identity,
		.nonce = installed.route_nonce,
		.active = 1U,
	};
	if (payload_mm_authvar_presence_lifecycle_close_endpoint_validate(
		&ready.endpoint) != CB_SUCCESS ||
	    payload_mm_authvar_presence_lifecycle_close_backing_return(
		&provider.backing, &ready) != CB_SUCCESS)
		goto out;
	provider.ready = ready;
	expected = PROVIDER_INSTALLING;
	if (!__atomic_compare_exchange_n(&provider.state, &expected,
		PROVIDER_READY, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		goto out;
	status = CB_SUCCESS;
out:
	scrub(&descriptor, sizeof(descriptor));
	scrub(&frozen, sizeof(frozen));
	scrub(&installed, sizeof(installed));
	scrub(&ready, sizeof(ready));
	scrub(&presence, sizeof(presence));
	if (status != CB_SUCCESS) {
		if (route_live)
			platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
		provider_fail();
	}
	return status;
}

bool platform_payload_mm_authvar_presence_lifecycle_close_ready_receipt(
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt *receipt)
{
	uint32_t expected = PROVIDER_READY;

	if (!receipt || !__atomic_compare_exchange_n(&provider.state, &expected,
		PROVIDER_DELIVERED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		if (receipt)
			scrub(receipt, sizeof(*receipt));
		return false;
	}
	*receipt = provider.ready;
	scrub(&provider.ready, sizeof(provider.ready));
	return true;
}

void payload_mm_authvar_presence_lifecycle_close_provider_commit(void)
{
	uint32_t expected = PROVIDER_DELIVERED;

	if (!__atomic_compare_exchange_n(&provider.state, &expected,
		PROVIDER_COMMITTED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
	scrub(&provider.backing, sizeof(provider.backing));
}

void __noreturn
payload_mm_authvar_presence_lifecycle_close_provider_fail_stop(void)
{
	__atomic_store_n(&provider.state, PROVIDER_FAILED, __ATOMIC_RELEASE);
	platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
}

#if ENV_TEST
void payload_mm_authvar_presence_lifecycle_close_provider_reset_test(void)
{
	scrub(&provider, sizeof(provider));
}
#endif
