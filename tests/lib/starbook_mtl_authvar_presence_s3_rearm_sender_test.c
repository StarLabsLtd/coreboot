/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <cpu/x86/smm_invocation_loader_identity.h>
#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_s3_rearm.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static bool receipt_ready;
static uint64_t response;
static unsigned int calls;
static int acpi_s3;
static uint32_t authoritative_lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
static enum cb_err authority_status = CB_SUCCESS;

void starbook_mtl_authvar_presence_s3_resume_test(void);

int romstage_handoff_is_resume(void)
{
	return acpi_s3;
}

enum cb_err mainboard_loader_instance_authority_lifecycle(uint32_t *lifecycle)
{
	assert(lifecycle);
	*lifecycle = authoritative_lifecycle;
	return authority_status;
}

void __noreturn die(const char *message, ...)
{
	(void)message;
	abort();
}

bool starbook_mtl_dma_receipt_provisioned(void)
{
	return receipt_ready;
}

uint64_t starbook_mtl_authvar_presence_s3_rearm_trigger_test(uint64_t request)
{
	calls++;
	assert(request == STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST);
	return response;
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	response = STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_SUCCESS;
	if (!strcmp(argv[1], "cold")) {
		starbook_mtl_authvar_presence_s3_resume_test();
		assert(!calls);
		return 0;
	}
	if (!strcmp(argv[1], "s3")) {
		acpi_s3 = 1;
		authoritative_lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
		receipt_ready = true;
		starbook_mtl_authvar_presence_s3_resume_test();
		assert(calls == 1);
		return 0;
	}
	if (!strcmp(argv[1], "acpi-s3-authority-cold"))
		acpi_s3 = 1;
	else if (!strcmp(argv[1], "acpi-cold-authority-s3"))
		authoritative_lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	else if (!strcmp(argv[1], "authority-error"))
		authority_status = CB_ERR;
	else if (strcmp(argv[1], "send"))
		return 2;
	if (strcmp(argv[1], "send")) {
		starbook_mtl_authvar_presence_s3_resume_test();
		abort();
	}
	assert(starbook_mtl_authvar_presence_s3_rearm_send() != CB_SUCCESS);
	assert(!calls);
	receipt_ready = true;
	assert(starbook_mtl_authvar_presence_s3_rearm_send() == CB_SUCCESS);
	assert(calls == 1);
	response ^= 1U;
	assert(starbook_mtl_authvar_presence_s3_rearm_send() != CB_SUCCESS);
	assert(calls == 2);
	return 0;
}
