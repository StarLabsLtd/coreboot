/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <boot/payload_mm_authvar_smm_bootstrap.h>

#define Q35_MOR_SMM_STACK_MINIMUM 0x4000U

_Static_assert(CONFIG_SMM_MODULE_STACK_SIZE >= Q35_MOR_SMM_STACK_MINIMUM,
	"Q35 MOR private SMI bootstrap requires a larger SMM stack");

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
	return !CONFIG(SMMSTORE) && CONFIG(PAYLOAD_MM_AUTHVAR_QEMU_PFLASH_BACKEND);
}

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
