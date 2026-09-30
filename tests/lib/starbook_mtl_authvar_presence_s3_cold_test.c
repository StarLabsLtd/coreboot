/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <assert.h>
#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_presence_s3_record.h>
#include <bootmem.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "authvar_presence_lifecycle_close_install.h"
#include "authvar_presence_s3_cold.h"

#undef assert
#define assert(condition) do { if (!(condition)) __builtin_trap(); } while (0)

#define RECORD_SIZE 4096U
#define TEST_COLD_POISONED 7U

static uint8_t record[RECORD_SIZE] __aligned(8);
static struct smm_invocation_loader_instance instance;
static struct payload_mm_authvar_presence_lifecycle_close_route route;
static struct payload_mm_authvar_presence_s3_facts activated;
static struct lb_authvar_presence_endpoint presence_endpoint;
static struct payload_mm_authvar_presence_backing presence_backing;
static struct payload_mm_authvar_presence_lifecycle_close_snapshot close_snapshot;
static unsigned int clear_calls;
static unsigned int activate_calls;
static unsigned int seal_calls;
static unsigned int poison_calls;
static unsigned int proof_calls;
static unsigned int rearm_suspend_calls;
static unsigned int fail_proof_at;
static bool loader_read_ok;
static bool presence_ok;
static bool close_ok;
static bool installed_ok;
static bool activate_ok;
static bool seal_ok;
static bool rearm_suspend_ok;
static jmp_buf fail_stop_jump;
static bool fail_stop_armed;

void mainboard_smi_sleep(u8 slp_typ);

static void fixtures_reset(void)
{
	memset(record, 0xa5, sizeof(record));
	memset(&route, 0, sizeof(route));
	memset(&activated, 0, sizeof(activated));
	memset(&instance, 0, sizeof(instance));
	instance.state = SMM_INVOCATION_LOADER_INSTANCE_READY;
	instance.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION;
	instance.size = sizeof(instance);
	instance.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	instance.loader_instance_nonce.low = 1U;
	presence_endpoint = (struct lb_authvar_presence_endpoint) {
		.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
		.size = sizeof(presence_endpoint),
		.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
		.header_size = sizeof(presence_endpoint),
		.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS,
		.generation = 11U,
		.communication_base = 0x120000U,
		.communication_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
		.message_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
		.transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
		.trigger_width = sizeof(uint8_t),
		.trigger_address = 0xb2U,
		.trigger_value = 0xe1U,
		.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
		.capability_size = LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
	};
	presence_backing = (struct payload_mm_authvar_presence_backing) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION,
		.size = sizeof(presence_backing),
		.base = 0x120000U,
		.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE,
		.generation = 11U,
		.tag = BM_MEM_RESERVED,
	};
	close_snapshot = (struct payload_mm_authvar_presence_lifecycle_close_snapshot) {
		.endpoint = {
			.tag = LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT,
			.size = sizeof(close_snapshot.endpoint),
			.revision = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_REVISION,
			.header_size = sizeof(close_snapshot.endpoint),
			.flags = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_REQUIRED_FLAGS,
			.generation = 11U,
			.communication_base = 0x121000U,
			.communication_size =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
			.message_size =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
			.transport =
				LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT_APM_IO8,
			.trigger_width = sizeof(uint8_t),
			.trigger_address = 0xb2U,
			.trigger_value = 0xfeU,
			.source_mask =
				LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK,
		},
		.backing_base = 0x121000U,
		.backing_bytes =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE,
	};
	clear_calls = 0;
	activate_calls = 0;
	seal_calls = 0;
	poison_calls = 0;
	proof_calls = 0;
	rearm_suspend_calls = 0;
	fail_proof_at = 0;
	loader_read_ok = true;
	presence_ok = true;
	close_ok = true;
	installed_ok = true;
	activate_ok = true;
	seal_ok = true;
	rearm_suspend_ok = false;
	fail_stop_armed = false;
	starbook_mtl_authvar_presence_s3_cold_reset_test();
}

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **view)
{
	*view = (const struct smm_invocation_runtime_view *)(uintptr_t)1U;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base, size_t size)
{
	(void)view;
	(void)base;
	(void)size;
	proof_calls++;
	return fail_proof_at && proof_calls == fail_proof_at ? CB_ERR : CB_SUCCESS;
}

void smm_get_authvar_s3_state_buffer(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)record;
	*size = sizeof(record);
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *source,
	struct smm_invocation_loader_instance *snapshot)
{
	if (!loader_read_ok || source != &instance)
		return CB_ERR;
	*snapshot = *source;
	return CB_SUCCESS;
}

size_t payload_mm_authvar_presence_s3_record_size(void)
{
	return 464U;
}

enum cb_err payload_mm_authvar_presence_s3_record_cold_clear(
	void *storage, size_t storage_size)
{
	assert(storage == record && storage_size == sizeof(record));
	clear_calls++;
	memset(storage, 0, storage_size);
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_s3_record_cold_activate(
	void *storage, size_t storage_size,
	const struct payload_mm_authvar_presence_s3_facts *facts)
{
	assert(storage == record && storage_size == sizeof(record));
	activate_calls++;
	if (!activate_ok)
		return CB_ERR;
	activated = *facts;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_s3_record_suspend_seal(
	void *storage, size_t storage_size,
	const struct payload_mm_authvar_presence_s3_facts *facts)
{
	assert(storage == record && storage_size == sizeof(record));
	seal_calls++;
	return seal_ok && !memcmp(facts, &activated, sizeof(*facts)) ?
		CB_SUCCESS : CB_ERR;
}

void payload_mm_authvar_presence_s3_record_poison(
	void *storage, size_t storage_size)
{
	assert(storage == record && storage_size == sizeof(record));
	poison_calls++;
}

enum cb_err payload_mm_authvar_presence_authority_closed_snapshot(
	struct lb_authvar_presence_endpoint *endpoint,
	struct payload_mm_authvar_presence_backing *backing,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context, size_t storage_context_size)
{
	(void)storage_context_size;
	if (!presence_ok || !storage_is_protected(storage_context, endpoint,
						     sizeof(*endpoint)) ||
	    !storage_is_protected(storage_context, backing, sizeof(*backing)))
		return CB_ERR;
	*endpoint = presence_endpoint;
	*backing = presence_backing;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_route_idle_snapshot(
	struct payload_mm_authvar_presence_lifecycle_close_route *source,
	struct payload_mm_authvar_presence_lifecycle_close_snapshot *snapshot,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context, size_t storage_context_size)
{
	(void)storage_context_size;
	if (!close_ok || source != &route ||
	    !storage_is_protected(storage_context, snapshot, sizeof(*snapshot)))
		return CB_ERR;
	*snapshot = close_snapshot;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_authvar_presence_lifecycle_close_installed_route(
	struct starbook_mtl_authvar_presence_lifecycle_close_installed_route *binding)
{
	if (!installed_ok)
		return CB_ERR;
	binding->route = &route;
	binding->retained_ops = NULL;
	return CB_SUCCESS;
}

void smm_invocation_platform_fail_stop(void)
{
	assert(fail_stop_armed);
	longjmp(fail_stop_jump, 1);
}

enum cb_err starbook_mtl_authvar_presence_s3_rearm_suspend(void)
{
	rearm_suspend_calls++;
	return rearm_suspend_ok ? CB_SUCCESS : CB_ERR;
}

static void install_and_activate(void)
{
	assert(starbook_mtl_authvar_presence_s3_cold_install(&instance) == CB_SUCCESS);
	assert(clear_calls == 1U);
	assert(starbook_mtl_authvar_presence_s3_cold_route_complete(&route) ==
		CB_SUCCESS);
	assert(activate_calls == 1U);
}

static void test_linear_cold_and_reproof(void)
{
	fixtures_reset();
	assert(starbook_mtl_authvar_presence_s3_cold_install(&instance) == CB_SUCCESS);
	assert(starbook_mtl_authvar_presence_s3_cold_install(&instance) == CB_ERR);
	assert(clear_calls == 1U);
	assert(starbook_mtl_authvar_presence_s3_cold_route_complete(&route) ==
		CB_SUCCESS);
	assert(activate_calls == 1U);
	assert(starbook_mtl_authvar_presence_s3_cold_route_complete(&route) ==
		CB_SUCCESS);
	assert(activate_calls == 1U);
	assert(activated.presence_endpoint.generation == 11U);
	assert(activated.close_endpoint.generation == 11U);
}

static void test_wrong_lifecycle_and_early_complete(void)
{
	fixtures_reset();
	instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	assert(starbook_mtl_authvar_presence_s3_cold_install(&instance) == CB_ERR);
	assert(clear_calls == 0U);
	fixtures_reset();
	assert(starbook_mtl_authvar_presence_s3_cold_route_complete(&route) == CB_ERR);
	assert(activate_calls == 0U);
	assert(poison_calls == 1U);
	assert(starbook_mtl_authvar_presence_s3_cold_state_test() ==
		TEST_COLD_POISONED);
}

static void test_exact_reproof_and_failures_poison(void)
{
	fixtures_reset();
	install_and_activate();
	close_snapshot.endpoint.generation++;
	assert(starbook_mtl_authvar_presence_s3_cold_route_complete(&route) == CB_ERR);
	assert(activate_calls == 1U);
	assert(poison_calls == 1U);
	assert(starbook_mtl_authvar_presence_s3_cold_state_test() ==
		TEST_COLD_POISONED);

	fixtures_reset();
	assert(starbook_mtl_authvar_presence_s3_cold_install(&instance) == CB_SUCCESS);
	activate_ok = false;
	assert(starbook_mtl_authvar_presence_s3_cold_route_complete(&route) == CB_ERR);
	assert(poison_calls == 1U);

	fixtures_reset();
	assert(starbook_mtl_authvar_presence_s3_cold_install(&instance) == CB_SUCCESS);
	presence_ok = false;
	assert(starbook_mtl_authvar_presence_s3_cold_route_complete(&route) == CB_ERR);
	assert(poison_calls == 1U);
}

static void expect_s3_fail_stop(void)
{
	fail_stop_armed = true;
	if (!setjmp(fail_stop_jump)) {
		mainboard_smi_sleep(ACPI_S3);
		assert(false);
	}
	fail_stop_armed = false;
}

static void test_sleep_hook(void)
{
	fixtures_reset();
	mainboard_smi_sleep(ACPI_S5);
	assert(seal_calls == 0U && rearm_suspend_calls == 0U && poison_calls == 0U);

	fixtures_reset();
	rearm_suspend_ok = true;
	mainboard_smi_sleep(ACPI_S3);
	assert(seal_calls == 0U && rearm_suspend_calls == 1U && poison_calls == 0U);

	fixtures_reset();
	expect_s3_fail_stop();
	assert(seal_calls == 0U && rearm_suspend_calls == 1U && poison_calls == 0U);

	fixtures_reset();
	install_and_activate();
	mainboard_smi_sleep(ACPI_S3);
	assert(seal_calls == 1U && rearm_suspend_calls == 0U && poison_calls == 0U);

	fixtures_reset();
	install_and_activate();
	presence_endpoint.generation++;
	expect_s3_fail_stop();
	assert(seal_calls == 0U && rearm_suspend_calls == 0U && poison_calls == 1U);

	fixtures_reset();
	install_and_activate();
	seal_ok = false;
	expect_s3_fail_stop();
	assert(seal_calls == 1U && rearm_suspend_calls == 0U && poison_calls == 1U);
}

static void test_protection_failure(void)
{
	fixtures_reset();
	fail_proof_at = 1U;
	assert(starbook_mtl_authvar_presence_s3_cold_install(&instance) == CB_ERR);
	assert(clear_calls == 0U);

	fixtures_reset();
	assert(starbook_mtl_authvar_presence_s3_cold_install(&instance) == CB_SUCCESS);
	fail_proof_at = proof_calls + 1U;
	assert(starbook_mtl_authvar_presence_s3_cold_route_complete(&route) == CB_ERR);
	assert(activate_calls == 0U && poison_calls == 1U);
}

int main(void)
{
	test_linear_cold_and_reproof();
	test_wrong_lifecycle_and_early_complete();
	test_exact_reproof_and_failures_poison();
	test_sleep_hook();
	test_protection_failure();
	return 0;
}
