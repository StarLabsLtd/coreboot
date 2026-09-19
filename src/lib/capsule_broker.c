/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>
#include <commonlib/helpers.h>
#include <stdint.h>
#include <string.h>

#include "capsule_broker_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Capsule broker runtime must only be built in SMM"
#endif

static struct {
	struct capsule_broker_policy policy;
	struct lb_capsule_update_region regions[CAPSULE_UPDATE_MAX_REGIONS];
	uint8_t media_context[CAPSULE_BROKER_CONTEXT_SIZE] __aligned(8);
	uint8_t sha256_context[CAPSULE_BROKER_CONTEXT_SIZE] __aligned(8);
	uint8_t proof_context[CAPSULE_BROKER_CONTEXT_SIZE] __aligned(8);
	uint64_t last_transaction;
	uint64_t grant_transaction;
	uint32_t grant_version;
	bool grant_valid;
	bool installed;
	bool install_attempted;
	bool closed;
} broker;

static uint64_t unpack64(lb_uint64_t value)
{
	return value;
}

static bool range_end(uint64_t base, uint64_t size, uint64_t *end)
{
	if (!size || base > UINT64_MAX - size)
		return false;
	*end = base + size;
	return true;
}

static bool ranges_overlap(uint64_t left_base, uint64_t left_size,
	uint64_t right_base, uint64_t right_size)
{
	uint64_t left_end;
	uint64_t right_end;

	if (!range_end(left_base, left_size, &left_end) ||
	    !range_end(right_base, right_size, &right_end))
		return true;
	return left_base < right_end && right_base < left_end;
}

bool capsule_broker_generation_matches(uint64_t generation)
{
	return broker.installed && !broker.closed &&
		generation == unpack64(broker.policy.endpoint.generation);
}

bool capsule_broker_intent_matches(uint64_t generation, uint64_t image_size)
{
	return capsule_broker_generation_matches(generation) &&
		image_size == unpack64(broker.policy.endpoint.staging_size);
}

bool capsule_broker_buffer_available(const void *buffer, size_t size)
{
	return !ranges_overlap((uintptr_t)buffer, size, (uintptr_t)&broker,
			sizeof(broker)) &&
		(!broker.installed ||
		 !ranges_overlap((uintptr_t)buffer, size,
			(uintptr_t)broker.policy.scratch,
			broker.policy.scratch_size));
}

static bool range_addressable(uint64_t base, uint64_t size)
{
	return size && base <= UINTPTR_MAX && size - 1 <= UINTPTR_MAX - base;
}

static bool bytes_zero(const uint8_t *data, size_t size)
{
	uint8_t bits = 0;

	for (size_t i = 0; i < size; i++)
		bits |= data[i];
	return bits == 0;
}

static bool endpoint_valid(const struct lb_capsule_broker_endpoint *endpoint,
	uint64_t image_size)
{
	uint64_t communication_base;
	uint64_t staging_base;
	uint64_t staging_size;

	if (!endpoint || endpoint->tag != LB_TAG_CAPSULE_BROKER_ENDPOINT ||
	    endpoint->size != sizeof(*endpoint) ||
	    endpoint->revision != LB_CAPSULE_BROKER_ENDPOINT_REVISION ||
	    endpoint->header_size != sizeof(*endpoint) ||
	    endpoint->flags != LB_CAPSULE_ENDPOINT_REQUIRED_FLAGS ||
	    endpoint->communication_size != sizeof(struct capsule_broker_message) ||
	    endpoint->message_size != sizeof(struct capsule_broker_message) ||
	    endpoint->transport != LB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8 ||
	    endpoint->trigger_width != sizeof(uint8_t) ||
	    !endpoint->trigger_address || endpoint->trigger_address > UINT16_MAX ||
	    !endpoint->trigger_value || endpoint->trigger_value > UINT8_MAX ||
	    endpoint->reserved[0] ||
	    endpoint->reserved[1] || endpoint->reserved[2])
		return false;
	communication_base = unpack64(endpoint->communication_base);
	staging_base = unpack64(endpoint->staging_base);
	staging_size = unpack64(endpoint->staging_size);
	if (!unpack64(endpoint->generation) || !communication_base ||
	    communication_base % sizeof(uint64_t) || !staging_base ||
	    staging_base % sizeof(uint64_t) || staging_size != image_size ||
	    !range_addressable(communication_base, endpoint->communication_size) ||
	    !range_addressable(staging_base, staging_size) ||
	    ranges_overlap(communication_base, endpoint->communication_size,
		staging_base, staging_size))
		return false;
	return true;
}

enum cb_err capsule_broker_endpoint_validate(
	const struct lb_capsule_broker_endpoint *endpoint,
	const struct lb_capsule_handoff *handoff)
{
	if (!handoff || handoff->tag != LB_TAG_CAPSULE_HANDOFF ||
	    handoff->revision != LB_CAPSULE_HANDOFF_REVISION ||
	    handoff->header_size != sizeof(*handoff))
		return CB_ERR;
	return endpoint_valid(endpoint, unpack64(handoff->image_size)) ?
		CB_SUCCESS : CB_ERR;
}

static bool proofs_present(const struct capsule_broker_proofs *proofs)
{
	return proofs->communication_reserved && proofs->staging_reserved &&
		proofs->dma_protected && proofs->smm_spi_owned &&
		proofs->no_raw_flash && proofs->cpu_rendezvous_active;
}

static bool context_valid(const void *context, size_t size)
{
	return (context == NULL && size == 0) ||
		(context != NULL && size && size <= CAPSULE_BROKER_CONTEXT_SIZE &&
		 !ranges_overlap((uintptr_t)context, size, (uintptr_t)&broker,
			sizeof(broker)));
}

static bool execution_guard(void)
{
	const struct capsule_broker_proofs *proofs = &broker.policy.proofs;
	uint64_t communication = unpack64(broker.policy.endpoint.communication_base);
	uint64_t staging = unpack64(broker.policy.endpoint.staging_base);
	uint64_t staging_size = unpack64(broker.policy.endpoint.staging_size);

	return proofs->dma_protected(proofs->context, communication,
		broker.policy.endpoint.communication_size) &&
		proofs->dma_protected(proofs->context, staging, staging_size) &&
		proofs->smm_spi_owned(proofs->context) &&
		proofs->no_raw_flash(proofs->context) &&
		proofs->cpu_rendezvous_active(proofs->context);
}

enum cb_err capsule_broker_policy_install(
	const struct capsule_broker_policy *trusted_policy,
	capsule_broker_protected_storage_fn storage_is_protected, void *context)
{
	struct capsule_broker_policy snapshot;
	uint64_t communication;
	uint64_t staging;
	uint64_t staging_size;

	if (broker.install_attempted)
		return CB_ERR;
	broker.install_attempted = true;
	if (!trusted_policy || !storage_is_protected ||
	    !storage_is_protected(context, &broker, sizeof(broker)))
		return CB_ERR;
	memcpy(&snapshot, trusted_policy, sizeof(snapshot));
	communication = unpack64(snapshot.endpoint.communication_base);
	staging = unpack64(snapshot.endpoint.staging_base);
	staging_size = unpack64(snapshot.endpoint.staging_size);
	if (snapshot.revision != CAPSULE_BROKER_POLICY_REVISION ||
	    snapshot.size != sizeof(snapshot) ||
	    !snapshot.image_size ||
	    !endpoint_valid(&snapshot.endpoint, snapshot.image_size) ||
	    !snapshot.boot_media_size || !snapshot.smmstore_size ||
	    snapshot.smmstore_offset > snapshot.boot_media_size ||
	    snapshot.smmstore_size > snapshot.boot_media_size -
		snapshot.smmstore_offset || !snapshot.erase_size ||
	    !snapshot.region_count ||
	    snapshot.region_count > CAPSULE_UPDATE_MAX_REGIONS ||
	    snapshot.media.size != snapshot.boot_media_size ||
	    snapshot.media.erase_size != snapshot.erase_size ||
	    !snapshot.media.read || !snapshot.media.erase || !snapshot.media.write ||
	    !context_valid(snapshot.media.context,
		snapshot.media_context_size) ||
	    !snapshot.scratch || snapshot.scratch_size < snapshot.erase_size ||
	    !snapshot.sha256 ||
	    !context_valid(snapshot.sha256_context,
		snapshot.sha256_context_size) ||
	    !proofs_present(&snapshot.proofs) ||
	    !context_valid(snapshot.proofs.context,
		snapshot.proofs.context_size))
		return CB_ERR;
	if (snapshot.media_context_size) {
		memcpy(broker.media_context, snapshot.media.context,
			snapshot.media_context_size);
		snapshot.media.context = broker.media_context;
	}
	if (snapshot.sha256_context_size) {
		memcpy(broker.sha256_context, snapshot.sha256_context,
			snapshot.sha256_context_size);
		snapshot.sha256_context = broker.sha256_context;
	}
	if (snapshot.proofs.context_size) {
		memcpy(broker.proof_context, snapshot.proofs.context,
			snapshot.proofs.context_size);
		snapshot.proofs.context = broker.proof_context;
	}
	if (!storage_is_protected(context, snapshot.scratch,
		snapshot.scratch_size) ||
	    !snapshot.proofs.communication_reserved(snapshot.proofs.context,
		communication, snapshot.endpoint.communication_size) ||
	    !snapshot.proofs.staging_reserved(snapshot.proofs.context, staging,
		staging_size) ||
	    !snapshot.proofs.dma_protected(snapshot.proofs.context, communication,
		snapshot.endpoint.communication_size) ||
	    !snapshot.proofs.dma_protected(snapshot.proofs.context, staging,
		staging_size) || !snapshot.proofs.smm_spi_owned(snapshot.proofs.context) ||
	    !snapshot.proofs.no_raw_flash(snapshot.proofs.context) ||
	    !snapshot.proofs.cpu_rendezvous_active(snapshot.proofs.context) ||
	    ranges_overlap(communication, snapshot.endpoint.communication_size,
		(uintptr_t)&broker, sizeof(broker)) ||
	    ranges_overlap(staging, staging_size, (uintptr_t)&broker,
		sizeof(broker)) ||
	    ranges_overlap((uintptr_t)snapshot.scratch, snapshot.scratch_size,
		(uintptr_t)&broker, sizeof(broker)) ||
	    ranges_overlap(communication, snapshot.endpoint.communication_size,
		(uintptr_t)snapshot.scratch, snapshot.scratch_size) ||
	    ranges_overlap(staging, staging_size, (uintptr_t)snapshot.scratch,
		snapshot.scratch_size))
		return CB_ERR;
	broker.policy = snapshot;
	memcpy(broker.regions, snapshot.regions,
		snapshot.region_count * sizeof(snapshot.regions[0]));
	broker.installed = true;
	return CB_SUCCESS;
}

enum cb_err capsule_broker_checkpoint_grant(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version)
{
	if (!broker.installed || broker.closed || broker.grant_valid ||
	    generation != unpack64(broker.policy.endpoint.generation) ||
	    !transaction || transaction <= broker.last_transaction)
		return CB_ERR;
	broker.grant_transaction = transaction;
	broker.grant_version = attempted_version;
	broker.grant_valid = true;
	return CB_SUCCESS;
}

static bool apply_shape_valid(const struct capsule_broker_message *message)
{
	return message->image_size == unpack64(broker.policy.endpoint.staging_size) &&
		message->digest_algorithm == CAPSULE_BROKER_DIGEST_SHA256 &&
		message->digest_size == CAPSULE_BROKER_DIGEST_SIZE;
}

static bool close_shape_valid(const struct capsule_broker_message *message)
{
	return !message->image_size && !message->digest_algorithm &&
		!message->digest_size &&
		bytes_zero(message->digest, sizeof(message->digest)) &&
		!message->attempted_version;
}

static bool common_shape_valid(const struct capsule_broker_message *message)
{
	return message->revision == CAPSULE_BROKER_MESSAGE_REVISION &&
		message->size == sizeof(*message) && !message->flags &&
		message->transaction && !message->reserved &&
		message->result == CAPSULE_BROKER_RESULT_PENDING &&
		message->last_attempt_status == CAPSULE_BROKER_STATUS_PENDING &&
		(message->operation == CAPSULE_BROKER_APPLY ||
		 message->operation == CAPSULE_BROKER_CLOSE);
}

static void respond(struct capsule_broker_message *message, uint32_t result,
	uint32_t status)
{
	void *shared = (void *)(uintptr_t)
		unpack64(broker.policy.endpoint.communication_base);

	message->result = result;
	message->last_attempt_status = status;
	memcpy(shared, message, sizeof(*message));
}

static enum cb_err guarded_read(void *context, u64 offset, void *data,
	size_t size)
{
	(void)context;
	if (!execution_guard())
		return CB_ERR;
	return broker.policy.media.read(broker.policy.media.context, offset,
		data, size);
}

static enum cb_err guarded_erase(void *context, u64 offset, size_t size)
{
	(void)context;
	if (!execution_guard())
		return CB_ERR;
	return broker.policy.media.erase(broker.policy.media.context, offset, size);
}

static enum cb_err guarded_write(void *context, u64 offset, const void *data,
	size_t size)
{
	(void)context;
	if (!execution_guard())
		return CB_ERR;
	return broker.policy.media.write(broker.policy.media.context, offset,
		data, size);
}

enum cb_err capsule_broker_handle(void)
{
	struct capsule_broker_message message;
	struct capsule_update_plan plan;
	struct capsule_media_policy media_policy;
	struct capsule_media_backend media;
	uint8_t digest[CAPSULE_BROKER_DIGEST_SIZE];
	void *shared;
	enum cb_err status;

	if (!broker.installed)
		return CB_ERR;
	shared = (void *)(uintptr_t)
		unpack64(broker.policy.endpoint.communication_base);
	memcpy(&message, shared, sizeof(message));
	if (broker.closed) {
		respond(&message, CAPSULE_BROKER_RESULT_CLOSED,
			CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL);
		return CB_ERR;
	}
	if (message.generation != unpack64(broker.policy.endpoint.generation)) {
		respond(&message, CAPSULE_BROKER_RESULT_REPLAY,
			CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL);
		return CB_ERR;
	}
	if (!common_shape_valid(&message) ||
	    message.transaction <= broker.last_transaction ||
	    (message.operation == CAPSULE_BROKER_APPLY &&
	     !apply_shape_valid(&message)) ||
	    (message.operation == CAPSULE_BROKER_CLOSE &&
	     !close_shape_valid(&message))) {
		broker.closed = true;
		broker.grant_valid = false;
		respond(&message, CAPSULE_BROKER_RESULT_INVALID,
			CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL);
		return CB_ERR;
	}
	broker.last_transaction = message.transaction;
	if (message.operation == CAPSULE_BROKER_CLOSE) {
		broker.closed = true;
		broker.grant_valid = false;
		respond(&message, CAPSULE_BROKER_RESULT_SUCCESS,
			CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS);
		return CB_SUCCESS;
	}
	if (!broker.grant_valid ||
	    broker.grant_transaction != message.transaction ||
	    broker.grant_version != message.attempted_version) {
		broker.closed = true;
		broker.grant_valid = false;
		respond(&message, CAPSULE_BROKER_RESULT_CHECKPOINT,
			CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL);
		return CB_ERR;
	}
	broker.closed = true;
	broker.grant_valid = false;
	if (!execution_guard() ||
	    broker.policy.sha256(broker.policy.sha256_context,
		(void *)(uintptr_t)unpack64(broker.policy.endpoint.staging_base),
		(size_t)message.image_size, digest) != CB_SUCCESS ||
	    !execution_guard() || memcmp(digest, message.digest, sizeof(digest))) {
		respond(&message, CAPSULE_BROKER_RESULT_DIGEST,
			CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL);
		return CB_ERR;
	}
	plan = (struct capsule_update_plan) {
		.image = (const void *)(uintptr_t)
			unpack64(broker.policy.endpoint.staging_base),
		.image_bytes = (size_t)message.image_size,
		.regions = broker.regions,
		.region_count = broker.policy.region_count,
	};
	media_policy = (struct capsule_media_policy) {
		.media_size = broker.policy.boot_media_size,
		.erase_size = broker.policy.erase_size,
		.smmstore_offset = broker.policy.smmstore_offset,
		.smmstore_size = broker.policy.smmstore_size,
		.regions = broker.regions,
		.region_count = broker.policy.region_count,
	};
	media = (struct capsule_media_backend) {
		.context = &broker,
		.size = broker.policy.media.size,
		.erase_size = broker.policy.media.erase_size,
		.read = guarded_read,
		.erase = guarded_erase,
		.write = guarded_write,
	};
	status = capsule_apply_policy_verified(&plan, &media_policy, &media,
		broker.policy.scratch, broker.policy.scratch_size);
	respond(&message, status == CB_SUCCESS ? CAPSULE_BROKER_RESULT_SUCCESS :
		CAPSULE_BROKER_RESULT_MEDIA,
		status == CB_SUCCESS ? CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS :
		CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL);
	return status;
}

void capsule_broker_close_for_s3(void)
{
	broker.closed = true;
	broker.grant_valid = false;
}
