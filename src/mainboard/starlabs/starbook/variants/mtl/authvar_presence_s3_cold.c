/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_s3_cold.h"
#include "authvar_presence_lifecycle_close_install.h"

#include <acpi/acpi.h>
#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_presence_s3_record.h>
#include <bootmem.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL authenticated-variable S3 cold record is SMM-only"
#endif

enum cold_state { COLD_EMPTY, COLD_CLEARING, COLD_CLEARED, COLD_ACTIVATING,
	COLD_ACTIVE, COLD_SEALING, COLD_SEALED, COLD_POISONED };

static struct {
	uint32_t state;
	uint32_t reserved;
	struct payload_mm_authvar_presence_s3_facts facts;
	struct payload_mm_authvar_presence_s3_facts sealed_facts;
} owner __aligned(8);

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool protected_storage(void *unused, const void *object, size_t size)
{
	const struct smm_invocation_runtime_view *view;

	(void)unused;
	return smm_invocation_runtime_view_get(&view) == CB_SUCCESS &&
		smm_invocation_runtime_range_is_protected(view, object, size) ==
			CB_SUCCESS;
}

static bool record_storage(void **storage, size_t *size)
{
	uintptr_t base;

	if (!storage || !size)
		return false;
	smm_get_authvar_s3_state_buffer(&base, size);
	*storage = (void *)base;
	return base && *size == CONFIG_SMM_AUTHVAR_S3_STATE_SMRAM_SIZE &&
		*size >= payload_mm_authvar_presence_s3_record_size() &&
		protected_storage(NULL, *storage, *size);
}

static void poison(void)
{
	void *storage;
	size_t size;

	if (record_storage(&storage, &size))
		payload_mm_authvar_presence_s3_record_poison(storage, size);
	scrub(&owner.facts, sizeof(owner.facts));
	scrub(&owner.sealed_facts, sizeof(owner.sealed_facts));
	__atomic_store_n(&owner.state, COLD_POISONED, __ATOMIC_RELEASE);
}

static enum cb_err facts_build(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	struct payload_mm_authvar_presence_s3_facts *facts)
{
	struct payload_mm_authvar_presence_lifecycle_close_snapshot close;
	struct payload_mm_authvar_presence_s3_facts value = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_S3_FACTS_REVISION,
		.size = sizeof(value),
		.close_backing_tag = BM_MEM_RESERVED,
		.presence_terminal = PAYLOAD_MM_AUTHVAR_PRESENCE_S3_CLOSED,
		.lifecycle_close_state =
			PAYLOAD_MM_AUTHVAR_PRESENCE_S3_LIFECYCLE_CLOSE_IDLE,
	};
	enum cb_err result = CB_ERR;

	if (!route || !facts || !protected_storage(NULL, facts, sizeof(*facts)) ||
	    payload_mm_authvar_presence_authority_closed_snapshot(
		&value.presence_endpoint, &value.presence_backing,
		protected_storage, NULL, 0U) != CB_SUCCESS ||
	    payload_mm_authvar_presence_lifecycle_close_route_idle_snapshot(
		route, &close, protected_storage, NULL, 0U) != CB_SUCCESS)
		goto out;
	value.close_endpoint = close.endpoint;
	value.close_backing_base = close.backing_base;
	value.close_backing_bytes = close.backing_bytes;
	if (value.presence_endpoint.generation != close.endpoint.generation ||
	    close.backing_base != close.endpoint.communication_base ||
	    close.backing_bytes !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE ||
	    !protected_storage(NULL, facts, sizeof(*facts)))
		goto out;
	*facts = value;
	if (memcmp(facts, &value, sizeof(value)) ||
	    !protected_storage(NULL, facts, sizeof(*facts))) {
		scrub(facts, sizeof(*facts));
		goto out;
	}
	result = CB_SUCCESS;
out:
	scrub(&close, sizeof(close));
	scrub(&value, sizeof(value));
	return result;
}

enum cb_err starbook_mtl_authvar_presence_s3_cold_install(
	const struct smm_invocation_loader_instance *instance)
{
	struct smm_invocation_loader_instance snapshot;
	void *storage;
	size_t size;
	uint32_t expected = COLD_EMPTY;

	if (!instance ||
	    smm_invocation_loader_instance_read(instance, &snapshot) != CB_SUCCESS ||
	    snapshot.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    !protected_storage(NULL, &owner, sizeof(owner)) ||
	    !record_storage(&storage, &size) ||
	    !__atomic_compare_exchange_n(&owner.state, &expected, COLD_CLEARING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		scrub(&snapshot, sizeof(snapshot));
		return CB_ERR;
	}
	if (payload_mm_authvar_presence_s3_record_cold_clear(storage, size) !=
	    CB_SUCCESS || !protected_storage(NULL, &owner, sizeof(owner))) {
		poison();
		scrub(&snapshot, sizeof(snapshot));
		return CB_ERR;
	}
	__atomic_store_n(&owner.state, COLD_CLEARED, __ATOMIC_RELEASE);
	scrub(&snapshot, sizeof(snapshot));
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_authvar_presence_s3_cold_route_complete(
	struct payload_mm_authvar_presence_lifecycle_close_route *route)
{
	struct payload_mm_authvar_presence_s3_facts facts = { 0 };
	void *storage;
	size_t size;
	uint32_t state;
	uint32_t expected;
	enum cb_err result = CB_ERR;

	if (!protected_storage(NULL, &owner, sizeof(owner)) ||
	    !record_storage(&storage, &size) || facts_build(route, &facts) !=
	    CB_SUCCESS || !protected_storage(NULL, &owner, sizeof(owner)))
		goto out;
	state = __atomic_load_n(&owner.state, __ATOMIC_ACQUIRE);
	if (state == COLD_ACTIVE) {
		if (!memcmp(&facts, &owner.facts, sizeof(facts)) &&
		    !memcmp(&facts, &owner.sealed_facts, sizeof(facts)))
			result = CB_SUCCESS;
		goto out;
	}
	expected = COLD_CLEARED;
	if (!__atomic_compare_exchange_n(&owner.state, &expected, COLD_ACTIVATING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    payload_mm_authvar_presence_s3_record_cold_activate(storage, size,
		&facts) != CB_SUCCESS ||
	    !protected_storage(NULL, &owner, sizeof(owner)))
		goto out;
	owner.facts = facts;
	owner.sealed_facts = facts;
	__atomic_store_n(&owner.state, COLD_ACTIVE, __ATOMIC_RELEASE);
	result = CB_SUCCESS;
out:
	if (result != CB_SUCCESS)
		poison();
	scrub(&facts, sizeof(facts));
	return result;
}

enum cb_err starbook_mtl_authvar_presence_s3_suspend(void)
{
	struct starbook_mtl_authvar_presence_lifecycle_close_installed_route installed;
	struct payload_mm_authvar_presence_s3_facts facts = { 0 };
	void *storage;
	size_t size;
	uint32_t expected = COLD_ACTIVE;
	enum cb_err result = CB_ERR;

	if (!protected_storage(NULL, &owner, sizeof(owner)) ||
	    !record_storage(&storage, &size) ||
	    starbook_mtl_authvar_presence_lifecycle_close_installed_route(
		&installed) != CB_SUCCESS ||
	    facts_build(installed.route, &facts) != CB_SUCCESS ||
	    memcmp(&facts, &owner.facts, sizeof(facts)) ||
	    memcmp(&facts, &owner.sealed_facts, sizeof(facts)) ||
	    !__atomic_compare_exchange_n(&owner.state, &expected, COLD_SEALING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    payload_mm_authvar_presence_s3_record_suspend_seal(storage, size,
		&facts) != CB_SUCCESS ||
	    !protected_storage(NULL, &owner, sizeof(owner)))
		goto out;
	__atomic_store_n(&owner.state, COLD_SEALED, __ATOMIC_RELEASE);
	result = CB_SUCCESS;
out:
	if (result != CB_SUCCESS)
		poison();
	scrub(&facts, sizeof(facts));
	scrub(&installed, sizeof(installed));
	return result;
}

void mainboard_smi_sleep(u8 slp_typ)
{
	if (slp_typ != ACPI_S3)
		return;
	if (starbook_mtl_authvar_presence_s3_suspend() != CB_SUCCESS)
		smm_invocation_platform_fail_stop();
}

#if ENV_TEST
void starbook_mtl_authvar_presence_s3_cold_reset_test(void)
{
	scrub(&owner, sizeof(owner));
}

uint32_t starbook_mtl_authvar_presence_s3_cold_state_test(void)
{
	return __atomic_load_n(&owner.state, __ATOMIC_ACQUIRE);
}
#endif
