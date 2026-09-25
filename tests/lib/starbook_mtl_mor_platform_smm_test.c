/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <boot/payload_mm_authvar_smm_bootstrap.h>
#include <string.h>

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static bool restricted;
static unsigned int bootstrap_calls;
static struct payload_mm_authvar_smm_bootstrap observed;

bool intel_smm_spi_writes_restricted(void)
{
	return restricted;
}

enum cb_err payload_mm_authvar_smm_bootstrap_install(
	const struct payload_mm_authvar_smm_bootstrap *bootstrap)
{
	bootstrap_calls++;
	observed = *bootstrap;
	return bootstrap->spi_writes_restricted_to_smm(bootstrap->spi_context) ?
		CB_SUCCESS : CB_ERR;
}

#include "../../src/mainboard/starlabs/starbook/variants/mtl/mor_platform_smm.c"

int main(void)
{
	struct payload_mm_authvar_mor_private_smi_slot slot = {
		.cold_boot_generation = 0x123456789abcdef0ULL,
	};
	struct payload_mm_authvar_mor_seal_channel channel = {
		.transport_base = 0x400100,
		.transport_size = 256,
		.caller = 0x9876,
		.caller_context = 0x5432,
	};

	assert(platform_payload_mm_authvar_mor_private_smi_bootstrap(NULL,
		&channel) == CB_ERR_ARG);
	assert(platform_payload_mm_authvar_mor_private_smi_bootstrap(&slot,
		NULL) == CB_ERR_ARG);
	assert(!bootstrap_calls);
	restricted = false;
	assert(platform_payload_mm_authvar_mor_private_smi_bootstrap(&slot,
		&channel) == CB_ERR);
	assert(bootstrap_calls == 1);
	assert(observed.revision == PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_REVISION);
	assert(observed.size == sizeof(observed));
	assert(observed.cold_boot_generation == slot.cold_boot_generation);
	assert(!memcmp(&observed.seal_channel, &channel, sizeof(channel)));
	assert(observed.spi_writes_restricted_to_smm && !observed.spi_context &&
		!observed.spi_context_size);
	restricted = true;
	assert(platform_payload_mm_authvar_mor_private_smi_bootstrap(&slot,
		&channel) == CB_SUCCESS);
	assert(bootstrap_calls == 2);
	return 0;
}
