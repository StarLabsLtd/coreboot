/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */

#include "authvar.h"
#include "authvar_dispatch.h"
#include <boot/authvar_media.h>
#include <boot_device.h>
#include <commonlib/coreboot_tables.h>
#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <fmap_config.h>
#include <intelblocks/fast_spi.h>
#include <smmstore.h>
#include <string.h>
#if CONFIG(DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT)
#include "capsule.h"
#endif
#if CONFIG(CDK2_SECURE_BOOT_FIXED_KEYS)
#include "fixed_policy_assets.h"
#include <cf9_reset.h>
#endif

_Static_assert(SMM_BLOCK_SIZE <= PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE &&
	SMM_BLOCK_SIZE >= PAYLOAD_MM_AUTHVAR_SERVICE_MIN_MESSAGE_SIZE,
	"authenticated-variable communication size");
_Static_assert(CONFIG_SMM_MODULE_STACK_SIZE >= 0x4000,
	"authenticated-variable verification requires a 16 KiB SMM stack");

/* The module loader reserves this retained tail in SMRAM with the handler. */
#define OWNER_WORKSPACE_SIZE (5U * AUTHVAR_OWNER_MAX_STORE_SIZE + \
	4U * PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_RECORDS * \
		sizeof(struct payload_mm_authvar_store_entry) + \
	PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_RECORDS * \
		sizeof(struct payload_mm_authvar_reclaim_copy) + \
	_Alignof(struct payload_mm_authvar_store_entry) + \
	_Alignof(struct payload_mm_authvar_reclaim_copy))

static struct {
	struct smm_authvar_dispatch service;
#if CONFIG(CDK2_SECURE_BOOT_FIXED_KEYS)
	struct fixed_policy_assets fixed;
#endif
	_Alignas(16) uint8_t owner_workspace[OWNER_WORKSPACE_SIZE];
	struct authvar_media media;
	struct region_device store;
	uint8_t image[CONFIG_SMMSTORE_SIZE];
	uint8_t candidate[CONFIG_SMMSTORE_SIZE];
	uint8_t verify[SMM_BLOCK_SIZE];
	uint8_t workspace[SMM_BLOCK_SIZE];
	_Alignas(8) uint8_t request[SMM_BLOCK_SIZE];
	_Alignas(8) uint8_t response[SMM_BLOCK_SIZE];
	struct lb_authvar_service_endpoint endpoint;
	enum { OWNER_UNINITIALIZED, OWNER_FAILED, OWNER_READY } state;
} private __attribute__((section(".smm_authvar")));

static uint64_t read_media(void *context, uint32_t offset, void *buffer, size_t size)
{
	struct region_device *store = context;

	return rdev_readat(store, buffer, offset, size) == size ?
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS : PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
}

static uint64_t write_media(void *context, uint32_t offset, const void *buffer, size_t size)
{
	struct region_device *store = context;
	bool restore_wp;
	ssize_t written;

	if (!fast_spi_smm_write_begin(&restore_wp))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	written = rdev_writeat(store, buffer, offset, size);
	fast_spi_smm_write_end(restore_wp);
	return written == size ? PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS :
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
}

static uint64_t erase_media(void *context, uint32_t offset, size_t size)
{
	struct region_device *store = context;
	bool restore_wp;
	ssize_t erased;

	if (!fast_spi_smm_write_begin(&restore_wp))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	erased = rdev_eraseat(store, offset, size);
	fast_spi_smm_write_end(restore_wp);
	return erased == size ? PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS :
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
}

static const struct authvar_media_io media_io = {
	.context = &private.store,
	.read = read_media,
	.write = write_media,
	.erase = erase_media,
};

static uint64_t commit(void *context, const void *store, size_t size,
	const struct payload_mm_authvar_write_plan *append, const void *record)
{
	return authvar_media_commit(context, &media_io, store, size, append, record);
}

static bool bind_media_device(void)
{
	const struct region region = {
		.offset = FMAP_SECTION_SMMSTORE_START,
		.size = FMAP_SECTION_SMMSTORE_SIZE,
	};

	/* Use compiled bounds and controller reads, not CBMEM FMAP or cached ROM. */
	return region_sz(&region) == sizeof(private.image) &&
		boot_device_rw_subregion(&region, &private.store) == 0;
}

static bool owner_ready(void)
{
	uintptr_t communication;
	size_t size;

	smm_get_smmstore_com_buffer(&communication, &size);
	/* Resume may reprobe SPI, but must never redirect the retained endpoint. */
	return private.state == OWNER_READY &&
		communication == private.endpoint.communication_base &&
		size == private.endpoint.communication_size && bind_media_device();
}

static bool initialize(void)
{
	uintptr_t communication;
	size_t communication_size;
	struct authvar_media *media = &private.media;
	uint64_t status;

	/* An uncertain recovery or mutation must never reopen initialization. */
	private.state = OWNER_FAILED;
	smm_get_smmstore_com_buffer(&communication, &communication_size);
	if (communication_size != sizeof(private.request) ||
	    !communication || (communication & 7U) ||
	    communication_size - 1U > UINTPTR_MAX - communication ||
	    smm_points_to_smram((void *)communication, communication_size) ||
	    !bind_media_device())
		return false;

	*media = (struct authvar_media) {
		.image = private.image,
		.candidate = private.candidate,
		.verify = private.verify,
		.workspace = private.workspace,
		.image_size = sizeof(private.image),
		.candidate_size = sizeof(private.candidate),
		.block_size = SMM_BLOCK_SIZE,
	};
	status = authvar_media_initialize(media, &media_io);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return false;
	size_t required = authvar_owner_workspace_size(media->store_size);

	if (!required || required > sizeof(private.owner_workspace))
		return false;
#if CONFIG(CDK2_SECURE_BOOT_FIXED_KEYS)
	cdk2_fixed_secure_boot_assets(&private.fixed);
	private.service.fixed = &private.fixed.policy;
#endif
	status = authvar_owner_init(&private.service.owner, media->store, media->store_size,
		commit, media, private.owner_workspace, sizeof(private.owner_workspace),
		CONFIG(CDK2_SECURE_BOOT), private.service.fixed, NULL);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return false;
#if CONFIG(CDK2_SECURE_BOOT_FIXED_KEYS)
	/* A durable startup dbx update must reset before publishing verified policy. */
	if (private.service.owner.restart_required)
		full_reset();
#endif
	smm_authvar_endpoint(&private.endpoint, communication, communication_size);
	if (payload_mm_authvar_service_endpoint_validate(&private.endpoint) != CB_SUCCESS)
		return false;
#if CONFIG(DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT)
	if (!smm_capsule_initialize())
		return false;
#endif
	private.state = OWNER_READY;
	return true;
}

uint64_t smm_authvar_store_uint_option(const char *name, uint32_t value)
{
	uint16_t *wide = (void *)private.request;
	size_t capacity = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE / sizeof(*wide);

	if (!name || !*name || !owner_ready() ||
	    private.service.owner.failed || private.service.owner.restart_required)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	for (size_t i = 0; i < capacity; i++) {
		wide[i] = (uint8_t)name[i];
		if (wide[i])
			continue;
		uint64_t status = smm_authvar_dispatch_option(&private.service, wide, value);

		memset(private.request, 0, PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE);
		return status;
	}
	memset(private.request, 0, PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE);
	return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
}

/* The caller holds the SMI lock, has parked every peer, and checked protection. */
uint32_t smm_authvar_exec(void)
{
	if (private.state == OWNER_UNINITIALIZED) {
		/* S3 must retain the owner, not create a new boot-services lifetime. */
		if (smm_authvar_is_s3_resume()) {
			private.state = OWNER_FAILED;
			return SMMSTORE_RET_FAILURE;
		}
		return initialize() ? SMMSTORE_RET_SUCCESS : SMMSTORE_RET_FAILURE;
	}
	if (!owner_ready())
		return SMMSTORE_RET_FAILURE;

	/* No shared length, pointer or policy byte survives this fixed-size copy. */
	memcpy(private.request, (void *)(uintptr_t)private.endpoint.communication_base,
		sizeof(private.request));
	if (smm_authvar_dispatch(&private.service, &private.endpoint, private.request,
		private.response, sizeof(private.request)) != CB_SUCCESS) {
		memset(private.request, 0, sizeof(private.request));
		memset(private.response, 0, sizeof(private.response));
		return SMMSTORE_RET_FAILURE;
	}
	memcpy((void *)(uintptr_t)private.endpoint.communication_base, private.response,
		sizeof(private.response));
	memset(private.request, 0, sizeof(private.request));
	memset(private.response, 0, sizeof(private.response));
	return SMMSTORE_RET_SUCCESS;
}

#if CONFIG(DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT)
uint32_t smm_authvar_capsule_exec(void)
{
	if (!owner_ready())
		return SMMSTORE_RET_FAILURE;
	return smm_capsule_exec(&private.service);
}
#endif
