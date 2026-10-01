/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <boot/payload_mm_authvar_smm_bootstrap.h>
#include <boot/payload_mm_authvar_presence_bootstrap.h>
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
static unsigned int service_calls;
static bool binding_available;
static struct payload_mm_authvar_presence_transaction_binding canonical_binding;

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

enum cb_err payload_mm_authvar_presence_bootstrap_binding_get(
	const struct payload_mm_authvar_presence_transaction_binding **output)
{
	if (!binding_available)
		return CB_ERR;
	*output = &canonical_binding;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_smm_service_bootstrap_install(
	const struct payload_mm_authvar_smm_bootstrap *bootstrap)
{
	service_calls++;
	observed = *bootstrap;
	return bootstrap->spi_writes_restricted_to_smm(bootstrap->spi_context) ?
		CB_SUCCESS : CB_ERR;
}

#if CONFIG(STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER) || \
	CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_platform_smm.c"
#endif

int main(void)
{
#if CONFIG(STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER)
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
#else
	assert(!bootstrap_calls);
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	assert(starbook_mtl_authvar_service_bootstrap_install() == CB_ERR);
	assert(!service_calls);
	binding_available = true;
	canonical_binding.generation = 0x7123456789abcdefULL;
	restricted = false;
	assert(starbook_mtl_authvar_service_bootstrap_install() == CB_ERR);
	assert(service_calls == 1);
	assert(observed.revision == PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_REVISION);
	assert(observed.size == sizeof(observed));
	assert(observed.cold_boot_generation == canonical_binding.generation);
	const struct payload_mm_authvar_mor_seal_channel empty_channel = { 0 };
	assert(!memcmp(&observed.seal_channel, &empty_channel, sizeof(empty_channel)));
	assert(observed.spi_writes_restricted_to_smm && !observed.spi_context &&
		!observed.spi_context_size);
	restricted = true;
	assert(starbook_mtl_authvar_service_bootstrap_install() == CB_SUCCESS);
	assert(service_calls == 2);
#else
	assert(!service_calls);
#endif
	return 0;
}
