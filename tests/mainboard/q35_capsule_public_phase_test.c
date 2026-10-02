/* SPDX-License-Identifier: GPL-2.0-only */

#include <types.h>
#include <cpu/x86/smm_command.h>

extern int dprintf(int descriptor, const char *format, ...);
#define check(condition) do { \
	if (!(condition)) { \
		dprintf(2, "PUBLIC_PHASE_ASSERT: %s\n", #condition); \
		__builtin_abort(); \
	} \
} while (0)

/* HOST callback models only: no hardware source or SMM authority is asserted. */
static struct { uint32_t phase; } service;
static struct { bool active; uint64_t request; } wave;
static uint8_t held_command;
static bool private_held;
static bool window_open;

static bool claim_current(void)
{
	return private_held;
}

static bool q35_public_service_current(uint8_t command)
{
	return command == held_command;
}

#if CONFIG(CAPSULE_BROKER_CONTRACT)
static bool capsule_broker_ram_window_open(void)
{
	return window_open;
}
#endif

/* Exact production enum and three admission bodies supplied by the runner. */
#include Q35_PUBLIC_PHASE_SOURCE

int main(void)
{
	service.phase = SERVICE_CAPSULE_EXECUTING;
	held_command = SMM_APMC_CAPSULE_BROKER;
	window_open = true;
#if CONFIG(CAPSULE_BROKER_CONTRACT)
	check(capsule_broker_ram_window_open());
#endif
	check(!platform_payload_mm_authvar_service_runtime_admitted());
	check(q35_capsule_service_current() == !!CONFIG(SMM_APMC_ROUTE_CAPSULE_BROKER));
	check(q35_capsule_ram_transaction_current() ==
		!!(CONFIG(SMM_APMC_ROUTE_CAPSULE_BROKER) && CONFIG(CAPSULE_BROKER_CONTRACT)));
	window_open = false;
	check(!q35_capsule_ram_transaction_current());
	check(q35_capsule_service_current() == !!CONFIG(SMM_APMC_ROUTE_CAPSULE_BROKER));
	service.phase = SERVICE_READY;
	check(!q35_capsule_service_current());
	service.phase = SERVICE_CAPSULE_EXECUTING;
	wave.active = true;
	check(!q35_capsule_service_current());
	wave.active = false;
	held_command = SMM_APMC_AUTHVAR_SERVICE;
	check(!q35_capsule_service_current());
	check(!platform_payload_mm_authvar_service_runtime_admitted());
	service.phase = SERVICE_EXECUTING;
	check(platform_payload_mm_authvar_service_runtime_admitted());
	held_command = SMM_APMC_CAPSULE_BROKER;
	check(!platform_payload_mm_authvar_service_runtime_admitted());
	held_command = 0;
	wave.request = SMM_APMC_AUTHVAR_SERVICE;
	private_held = true;
	check(platform_payload_mm_authvar_service_runtime_admitted());
	wave.request = SMM_APMC_CAPSULE_BROKER;
	check(!platform_payload_mm_authvar_service_runtime_admitted());
	return 0;
}
