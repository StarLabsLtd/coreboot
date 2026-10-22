/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */

#include "capsule.h"
#include "capsule_image.h"
#include "capsule_policy.h"
#include "authvar_dispatch.h"
#include <boot/capsule_broker.h>
#include <boot_device.h>
#include <commonlib/bsd/fmap_serialized.h>
#include <cpu/x86/smm.h>
#include <drivers/efi/firmware_info.h>
#include <endian.h>
#include <fmap_config.h>
#include <intelblocks/fast_spi.h>
#include <smmstore.h>
#include <string.h>

extern const uint8_t cdk2_system_fmp_certificates[];
extern const size_t cdk2_system_fmp_certificates_size;

static const uint16_t state_name[] = { 'F', 'm', 'p', 'S', 't', 'a', 't', 'e', 0 };

static struct {
	struct smm_capsule_image image;
	struct lb_efi_fw_info firmware;
	struct region_device flash;
	struct lb_capsule_update_region regions[SMM_CAPSULE_MAX_REGIONS];
	uint8_t fmap[FMAP_SIZE];
	uint8_t write[SMM_CAPSULE_ERASE_SIZE], verify[SMM_CAPSULE_ERASE_SIZE];
	uint8_t request[CAPSULE_BROKER_TRANSPORT_SIZE] __aligned(8);
	uint8_t response[CAPSULE_BROKER_TRANSPORT_SIZE] __aligned(8);
	uintptr_t communication, staging;
	size_t staging_size;
	uint64_t transaction;
	uint32_t region_count;
	bool ready, closed, spent;
} capsule __attribute__((section(".smm_authvar")));

bool smm_capsule_state_name(const uint16_t *name, const void *guid)
{
	return !memcmp(guid, capsule.firmware.guid, 16) &&
		!memcmp(name, state_name, sizeof(state_name));
}

void smm_capsule_close(void)
{
	capsule.closed = true;
	smm_capsule_image_close(&capsule.image);
}

static bool bind_flash(void)
{
	const struct region whole = { .offset = 0, .size = CONFIG_ROM_SIZE };

	return boot_device_rw_subregion(&whole, &capsule.flash) == 0;
}

bool smm_capsule_initialize(void)
{
	size_t communication_size;
	uint32_t root_size;

	if (capsule.ready || smm_authvar_is_s3_resume())
		return false;
	smm_get_smmstore_com_buffer(&capsule.communication, &communication_size);
	smm_get_capsule_staging(&capsule.staging, &capsule.staging_size);
	if (communication_size < sizeof(capsule.request) || !capsule.staging ||
	    capsule.staging_size != SMM_CAPSULE_IMAGE_MAX_SIZE ||
	    capsule.staging > UINTPTR_MAX - capsule.staging_size ||
	    smm_points_to_smram((void *)capsule.staging, capsule.staging_size) ||
	    (capsule.communication < capsule.staging + capsule.staging_size &&
	     capsule.staging < capsule.communication + communication_size) || !bind_flash() ||
	    rdev_readat(&capsule.flash, capsule.fmap, FMAP_OFFSET, sizeof(capsule.fmap)) !=
		 sizeof(capsule.fmap) ||
	    !smm_capsule_regions(capsule.fmap, sizeof(capsule.fmap),
		capsule.regions, &capsule.region_count) ||
	    !efi_firmware_info(&capsule.firmware) ||
	    !capsule.firmware.version ||
	    capsule.firmware.lowest_supported_version > capsule.firmware.version ||
	    cdk2_system_fmp_certificates_size < 4)
		return false;
	root_size = be32dec(cdk2_system_fmp_certificates);
	if (!root_size || root_size > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE ||
	    ALIGN_UP(root_size, 4U) != cdk2_system_fmp_certificates_size - 4)
		return false;
	capsule.ready = true;
	return true;
}

static bool read_state(struct smm_authvar_dispatch *service, uint8_t state[20])
{
	uint32_t attributes = 0;
	size_t size = 20;
	uint64_t status;

	memset(state, 0, size);
	status = authvar_owner_get(&service->owner, state_name, capsule.firmware.guid,
		&attributes, &size, state);
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND)
		return true;
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS || size != 20 || attributes != 3)
		return false;
	for (size_t i = 0; i < 4; i++)
		if (state[i] > 1)
			return false;
	return true;
}

static bool commit_state(struct smm_authvar_dispatch *service, const uint8_t state[20])
{
	size_t reclaimed;

	return authvar_owner_set(&service->owner, state_name, capsule.firmware.guid,
		PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES, 20, state, false, service->fixed,
		&reclaimed) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static uint32_t state_floor(const uint8_t state[20])
{
	return MAX(capsule.firmware.lowest_supported_version,
		state[1] ? le32dec(state + 8) : 0);
}

static bool apply_image(const struct capsule_broker_raw_image *raw)
{
	for (size_t i = 0; i < capsule.region_count; i++) {
		const struct lb_capsule_update_region *region = &capsule.regions[i];

		for (uint64_t done = 0; done < region->size; done += sizeof(capsule.write)) {
			size_t offset = region->flash_offset + done;
			bool restore_wp;
			bool written;

			/* Copy and authenticate the complete erase unit before its first write. */
			if (!smm_capsule_image_read(&capsule.image,
				raw->offset + region->image_offset + done,
				capsule.write, sizeof(capsule.write)) ||
			    rdev_readat(&capsule.flash, capsule.verify, offset,
				sizeof(capsule.verify)) != sizeof(capsule.verify))
				return false;
			if (!memcmp(capsule.write, capsule.verify, sizeof(capsule.write)))
				continue;
			if (!fast_spi_smm_write_begin(&restore_wp))
				return false;
			written = rdev_eraseat(&capsule.flash, offset, sizeof(capsule.write)) ==
				sizeof(capsule.write) &&
				rdev_writeat(&capsule.flash, capsule.write, offset,
					sizeof(capsule.write)) == sizeof(capsule.write);
			fast_spi_smm_write_end(restore_wp);
			if (!written || rdev_readat(&capsule.flash, capsule.verify, offset,
				sizeof(capsule.verify)) != sizeof(capsule.verify) ||
			    memcmp(capsule.write, capsule.verify, sizeof(capsule.write)))
				return false;
		}
	}
	return true;
}

static void execute(struct smm_authvar_dispatch *service,
	const struct payload_mm_fmp_capsule_intent *intent,
	struct capsule_broker_transport_result *result)
{
	struct capsule_broker_raw_image raw;
	struct payload_mm_crypto_span root = {
		cdk2_system_fmp_certificates + 4, be32dec(cdk2_system_fmp_certificates),
	};
	uint8_t state[20];
	enum payload_mm_verify_status status;
	enum capsule_broker_authentication_status policy;
	bool set = intent->operation == PAYLOAD_MM_FMP_CAPSULE_SET;
	bool recorded = false;
	uint32_t outcome = CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL;

	if (capsule.closed || capsule.spent || service->owner.runtime ||
	    !read_state(service, state))
		return;
	if (set)
		capsule.spent = true;
	status = smm_capsule_image_open(&capsule.image, (void *)capsule.staging,
		intent->capsule_size, intent->digest, &service->owner.crypto, &root);
	if (status != PAYLOAD_MM_VERIFY_OK) {
		if (status == PAYLOAD_MM_VERIFY_MALFORMED ||
		    status == PAYLOAD_MM_VERIFY_UNSUPPORTED) {
			result->result = CAPSULE_BROKER_RESULT_IMAGE_REFUSED;
			outcome = CAPSULE_BROKER_LAST_ATTEMPT_INVALID_FORMAT;
			goto refusal;
		}
		if (status != PAYLOAD_MM_VERIFY_REJECTED)
			goto out;
		result->result = CAPSULE_BROKER_RESULT_SIGNATURE_REFUSED;
		outcome = set ? CAPSULE_BROKER_LAST_ATTEMPT_AUTH_ERROR :
			CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL;
		goto refusal;
	}
	policy = smm_capsule_policy(&capsule.image, capsule.firmware.guid,
		capsule.firmware.version, state_floor(state), intent->attempted_version,
		capsule.fmap, sizeof(capsule.fmap), &raw);
	if (policy != CAPSULE_BROKER_AUTHENTICATED) {
		if (policy == CAPSULE_BROKER_VERSION_REFUSED) {
			result->result = CAPSULE_BROKER_RESULT_VERSION_REFUSED;
			outcome = CAPSULE_BROKER_LAST_ATTEMPT_VERSION_TOO_LOW;
		} else {
			result->result = CAPSULE_BROKER_RESULT_IMAGE_REFUSED;
			outcome = CAPSULE_BROKER_LAST_ATTEMPT_INVALID_FORMAT;
		}
		goto refusal;
	}
	if (!set) {
		result->result = CAPSULE_BROKER_RESULT_SUCCESS;
		outcome = CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS;
		goto out;
	}
	/* This checkpoint deliberately survives any interrupted firmware mutation. */
	state[2] = state[3] = 1;
	le32enc(state + 12, CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL);
	le32enc(state + 16, intent->attempted_version);
	if (!commit_state(service, state))
		goto out;
	recorded = true;
	if (!apply_image(&raw))
		goto out;
	state[0] = state[1] = 1;
	le32enc(state + 4, intent->attempted_version);
	le32enc(state + 8, MAX(state_floor(state), raw.lowest_supported_version));
	le32enc(state + 12, CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS);
	if (!commit_state(service, state))
		goto out;
	result->result = CAPSULE_BROKER_RESULT_SUCCESS;
	outcome = CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS;
	goto out;
refusal:
	if (set) {
		state[2] = state[3] = 1;
		le32enc(state + 12, outcome);
		le32enc(state + 16, intent->attempted_version);
		if (!commit_state(service, state)) {
			result->result = CAPSULE_BROKER_RESULT_EXECUTION;
			outcome = CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL;
		}
	}
out:
	if (set && !recorded && result->result == CAPSULE_BROKER_RESULT_EXECUTION &&
	    !service->owner.failed) {
		state[2] = state[3] = 1;
		le32enc(state + 12, CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL);
		le32enc(state + 16, intent->attempted_version);
		commit_state(service, state);
	}
	result->last_attempt_status = outcome;
	smm_capsule_image_close(&capsule.image);
	if (set)
		capsule.closed = true;
	memset(capsule.write, 0, sizeof(capsule.write));
	memset(capsule.verify, 0, sizeof(capsule.verify));
}

uint32_t smm_capsule_exec(struct smm_authvar_dispatch *service)
{
	struct capsule_broker_transport_request *request = (void *)capsule.request;
	struct payload_mm_fmp_capsule_intent *intent =
		(void *)(capsule.request + CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET);
	struct capsule_broker_transport_result *result =
		(void *)(capsule.response + CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET);
	uint8_t state[20];
	uintptr_t staging;
	size_t staging_size;

	smm_get_capsule_staging(&staging, &staging_size);
	if (!capsule.ready || service->owner.failed || service->owner.restart_required ||
	    staging != capsule.staging || staging_size != capsule.staging_size ||
	    !bind_flash())
		return SMMSTORE_RET_FAILURE;
	memcpy(capsule.request, (void *)capsule.communication, sizeof(capsule.request));
	if (request->revision != CAPSULE_BROKER_TRANSPORT_RAM_REVISION ||
	    request->size != sizeof(*request) || request->flags ||
	    request->generation != 1 || !request->transaction ||
	    (request->operation != CAPSULE_BROKER_TRANSPORT_CLOSE_RAM &&
	     request->transaction <= capsule.transaction))
		return SMMSTORE_RET_FAILURE;
	/* Independent clients may close; closure grants no replayable authority. */
	if (request->operation != CAPSULE_BROKER_TRANSPORT_CLOSE_RAM)
		capsule.transaction = request->transaction;
	memset(capsule.response, 0, sizeof(capsule.response));
	memcpy(capsule.response, request, sizeof(*request));
	*result = (struct capsule_broker_transport_result) {
		.revision = request->revision, .size = sizeof(*result),
		.generation = request->generation, .transaction = request->transaction,
		.result = CAPSULE_BROKER_RESULT_EXECUTION,
		.last_attempt_status = CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL,
	};
	if (request->operation == CAPSULE_BROKER_TRANSPORT_EXECUTE &&
	    request->intent_size == sizeof(*intent) && request->result_size == sizeof(*result) &&
	    intent->revision == PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION &&
	    intent->size == sizeof(*intent) && !intent->flags && !intent->reserved &&
	    intent->broker_generation == request->generation &&
	    intent->transaction == request->transaction &&
	    intent->capsule_size && intent->capsule_size <= capsule.staging_size &&
	    intent->digest_algorithm == CAPSULE_BROKER_DIGEST_SHA256 &&
	    intent->digest_size == CAPSULE_BROKER_DIGEST_SIZE &&
	    (intent->operation == PAYLOAD_MM_FMP_CAPSULE_CHECK ||
	     intent->operation == PAYLOAD_MM_FMP_CAPSULE_SET)) {
		result->attempted_version = intent->attempted_version;
		memcpy(capsule.response + CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET,
			intent, sizeof(*intent));
		execute(service, intent, result);
	} else if (request->operation == CAPSULE_BROKER_TRANSPORT_CLOSE_RAM &&
		   !request->intent_size && request->result_size == sizeof(*result)) {
		smm_capsule_close();
		result->last_attempt_status = 0;
		result->result = CAPSULE_BROKER_RESULT_SUCCESS;
	} else if (request->operation == CAPSULE_BROKER_TRANSPORT_READ_INFO &&
		   !request->intent_size &&
		   request->result_size == sizeof(struct capsule_broker_transport_info)) {
		struct capsule_broker_transport_info *info =
			(void *)(capsule.response + CAPSULE_BROKER_TRANSPORT_INFO_OFFSET);

		*info = (struct capsule_broker_transport_info) {
			.revision = request->revision, .size = sizeof(*info),
			.generation = request->generation, .transaction = request->transaction,
			.result = CAPSULE_BROKER_RESULT_EXECUTION,
		};
		if (!service->owner.runtime && read_state(service, state)) {
			memcpy(&info->image_type, capsule.firmware.guid, sizeof(info->image_type));
			info->current_version = capsule.firmware.version;
			info->lowest_supported_version = state_floor(state);
			info->image_size = CONFIG_ROM_SIZE;
			info->capabilities = LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES;
			info->state_flags = (state[2] ? 1U : 0U) | (state[3] ? 2U : 0U);
			info->last_attempt_status = state[2] ? le32dec(state + 12) : 0;
			info->last_attempt_version = state[3] ? le32dec(state + 16) : 0;
			info->result = CAPSULE_BROKER_RESULT_SUCCESS;
		}
	}
	/* The caller owns the mailbox; publish its completion word last. */
	memcpy((void *)capsule.communication, capsule.response, sizeof(capsule.response) - 4);
	__atomic_store_n((uint32_t *)(capsule.communication + sizeof(capsule.response) - 4),
		*(uint32_t *)(capsule.response + sizeof(capsule.response) - 4), __ATOMIC_RELEASE);
	memset(capsule.request, 0, sizeof(capsule.request));
	memset(capsule.response, 0, sizeof(capsule.response));
	return SMMSTORE_RET_SUCCESS;
}
