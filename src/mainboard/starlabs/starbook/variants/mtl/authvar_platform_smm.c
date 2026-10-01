/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_platform_smm.h"
#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <boot/payload_mm_authvar_smm_bootstrap.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
#include <boot/payload_mm_authvar_presence_bootstrap.h>
#endif
#include <intelblocks/smm_spi_window.h>

#define STARBOOK_MTL_AUTHVAR_SMM_STACK_MINIMUM 0x4000U

_Static_assert(CONFIG_SMM_MODULE_STACK_SIZE >=
	STARBOOK_MTL_AUTHVAR_SMM_STACK_MINIMUM,
	"StarBook MTL authenticated-variable bootstrap requires a larger SMM stack");

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool writes_are_private(void *unused)
{
	(void)unused;
	return intel_smm_spi_writes_restricted();
}

#if CONFIG(STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER)
enum cb_err platform_payload_mm_authvar_mor_private_smi_bootstrap(
	struct payload_mm_authvar_mor_private_smi_slot *protected_slot,
	const struct payload_mm_authvar_mor_seal_channel *seal_channel)
{
	struct payload_mm_authvar_smm_bootstrap bootstrap;
	enum cb_err status;

	if (!protected_slot || !seal_channel)
		return CB_ERR_ARG;
	bootstrap = (struct payload_mm_authvar_smm_bootstrap) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_REVISION,
		.size = sizeof(bootstrap),
		.cold_boot_generation = protected_slot->cold_boot_generation,
		.seal_channel = *seal_channel,
		.spi_writes_restricted_to_smm = writes_are_private,
	};
	status = payload_mm_authvar_smm_bootstrap_install(&bootstrap);
	scrub(&bootstrap, sizeof(bootstrap));
	return status;
}
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
enum cb_err starbook_mtl_authvar_service_bootstrap_install(void)
{
	const struct payload_mm_authvar_presence_transaction_binding *binding;
	struct payload_mm_authvar_smm_bootstrap bootstrap;
	enum cb_err status;

	if (payload_mm_authvar_presence_bootstrap_binding_get(&binding) != CB_SUCCESS)
		return CB_ERR;
	bootstrap = (struct payload_mm_authvar_smm_bootstrap) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_REVISION,
		.size = sizeof(bootstrap),
		.cold_boot_generation = binding->generation,
		.spi_writes_restricted_to_smm = writes_are_private,
	};
	status = payload_mm_authvar_smm_service_bootstrap_install(&bootstrap);
	scrub(&bootstrap, sizeof(bootstrap));
	return status;
}
#endif
