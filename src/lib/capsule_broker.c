/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>
#include <commonlib/helpers.h>
#include <stdint.h>
#include <string.h>

#include "capsule_broker_internal.h"
#include "payload_mm_fmp_dispatch_internal.h"
#include "payload_mm_fmp_owner_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Capsule broker runtime must only be built in SMM"
#endif

static struct {
	struct capsule_broker_policy policy;
	struct lb_capsule_update_region regions[CAPSULE_UPDATE_MAX_REGIONS];
	uint8_t media_context[CAPSULE_BROKER_CONTEXT_SIZE] __aligned(8);
	uint8_t sha256_context[CAPSULE_BROKER_CONTEXT_SIZE] __aligned(8);
	uint8_t authenticate_context[CAPSULE_BROKER_CONTEXT_SIZE] __aligned(8);
	uint8_t proof_context[CAPSULE_BROKER_CONTEXT_SIZE] __aligned(8);
	uint64_t last_transaction;
	uint64_t grant_transaction;
	uint32_t grant_version;
	uint8_t grant_digest[CAPSULE_BROKER_DIGEST_SIZE];
	struct capsule_broker_raw_image grant_raw_image;
	uint64_t authenticated_transaction;
	uint64_t authenticated_owner_sequence;
	uint64_t grant_owner_sequence;
	uint64_t success_transaction;
	uint64_t success_owner_sequence;
	uint32_t authenticated_version;
	uint32_t success_version;
	uint32_t success_lowest_supported_version;
	uint8_t authenticated_digest[CAPSULE_BROKER_DIGEST_SIZE];
	uint8_t success_digest[CAPSULE_BROKER_DIGEST_SIZE];
	struct capsule_broker_raw_image authenticated_raw_image;
	struct capsule_broker_flash_plan flash_plan;
	bool grant_valid;
	bool authentication_valid;
	bool success_valid;
	bool authentication_in_progress;
	bool flash_plan_valid;
	bool installed;
	bool install_attempted;
	bool closed;
} broker;

#if ENV_TEST
const void *capsule_broker_test_authority(size_t *size)
{
	*size = sizeof(broker);
	return &broker;
}
#endif

static void clear_flash_plan(void)
{
	broker.flash_plan_valid = false;
	memset(&broker.flash_plan, 0, sizeof(broker.flash_plan));
}

static void clear_authentication(void)
{
	broker.authentication_valid = false;
	broker.authenticated_transaction = 0;
	broker.authenticated_owner_sequence = 0;
	broker.authenticated_version = 0;
	memset(broker.authenticated_digest, 0,
		sizeof(broker.authenticated_digest));
	memset(&broker.authenticated_raw_image, 0,
		sizeof(broker.authenticated_raw_image));
}

static void clear_success(void)
{
	broker.success_valid = false;
	broker.success_transaction = 0;
	broker.success_owner_sequence = 0;
	broker.success_version = 0;
	broker.success_lowest_supported_version = 0;
	memset(broker.success_digest, 0, sizeof(broker.success_digest));
}

static void clear_grant(void)
{
	broker.grant_valid = false;
	broker.grant_transaction = 0;
	broker.grant_owner_sequence = 0;
	broker.grant_version = 0;
	memset(broker.grant_digest, 0, sizeof(broker.grant_digest));
	memset(&broker.grant_raw_image, 0, sizeof(broker.grant_raw_image));
}

static bool authentication_state_valid(void)
{
	return broker.installed && !broker.closed && !broker.grant_valid &&
		!broker.authentication_valid && broker.authentication_in_progress;
}

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

bool capsule_broker_intent_matches(uint64_t generation, uint64_t capsule_size)
{
	return capsule_broker_generation_matches(generation) &&
		capsule_size &&
		capsule_size <= unpack64(broker.policy.endpoint.staging_size);
}

bool capsule_broker_buffer_available(const void *buffer, size_t size)
{
	return !ranges_overlap((uintptr_t)buffer, size, (uintptr_t)&broker,
			sizeof(broker)) &&
		(!broker.installed ||
		 !ranges_overlap((uintptr_t)buffer, size,
			(uintptr_t)broker.policy.scratch,
			broker.policy.scratch_size)) &&
		(!broker.installed ||
		 !ranges_overlap((uintptr_t)buffer, size,
			(uintptr_t)broker.policy.write_scratch,
			broker.policy.write_scratch_size));
}

static bool range_addressable(uint64_t base, uint64_t size)
{
	return size && base <= UINTPTR_MAX && size - 1 <= UINTPTR_MAX - base;
}

static bool endpoint_valid(const struct lb_capsule_broker_endpoint *endpoint,
	uint64_t image_size)
{
	uint64_t communication_base;
	uint64_t staging_base;
	uint64_t staging_size;

	if (!endpoint || !image_size ||
	    endpoint->tag != LB_TAG_CAPSULE_BROKER_ENDPOINT ||
	    endpoint->size != sizeof(*endpoint) ||
	    endpoint->revision != LB_CAPSULE_BROKER_ENDPOINT_REVISION ||
	    endpoint->header_size != sizeof(*endpoint) ||
	    endpoint->flags != LB_CAPSULE_ENDPOINT_REQUIRED_FLAGS ||
	    endpoint->communication_size != CAPSULE_BROKER_TRANSPORT_SIZE ||
	    endpoint->message_size != CAPSULE_BROKER_TRANSPORT_SIZE ||
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
	    staging_base % sizeof(uint64_t) || staging_size < image_size ||
	    !range_addressable(communication_base, endpoint->communication_size) ||
	    !range_addressable(staging_base, staging_size) ||
	    ranges_overlap(communication_base, endpoint->communication_size,
		staging_base, staging_size))
		return false;
	return true;
}

bool capsule_broker_transport_buffer(void **buffer, size_t *size,
	uint64_t *generation)
{
	if (!buffer || !size || !generation || !broker.installed || broker.closed)
		return false;
	*buffer = (void *)(uintptr_t)
		unpack64(broker.policy.endpoint.communication_base);
	*size = broker.policy.endpoint.communication_size;
	*generation = unpack64(broker.policy.endpoint.generation);
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

static bool policy_storage_disjoint(const struct capsule_broker_policy *policy,
	uint64_t communication, uint64_t staging, uint64_t staging_size)
{
	const void *storage[] = {
		policy->scratch,
		policy->write_scratch,
		policy->media.context,
		policy->sha256_context,
		policy->authenticate_context,
		policy->proofs.context,
	};
	const size_t size[] = {
		policy->scratch_size,
		policy->write_scratch_size,
		policy->media_context_size,
		policy->sha256_context_size,
		policy->authenticate_context_size,
		policy->proofs.context_size,
	};

	for (size_t i = 0; i < ARRAY_SIZE(storage); i++) {
		if (!size[i])
			continue;
		if (ranges_overlap((uintptr_t)storage[i], size[i], communication,
			policy->endpoint.communication_size) ||
		    ranges_overlap((uintptr_t)storage[i], size[i], staging,
			staging_size))
			return false;
		for (size_t j = i + 1; j < ARRAY_SIZE(storage); j++)
			if (size[j] && ranges_overlap((uintptr_t)storage[i], size[i],
				(uintptr_t)storage[j], size[j]))
				return false;
	}
	return true;
}

static bool fmap_name_valid(const struct fmap_area *area)
{
	return area->name[0] &&
		memchr(area->name, '\0', sizeof(area->name)) != NULL;
}

static bool fmap_policy_valid(const struct capsule_broker_policy *policy)
{
	if (!policy->fmap_area_count ||
	    policy->fmap_area_count > CAPSULE_BROKER_MAX_FMAP_AREAS)
		return false;
	for (size_t i = 0; i < policy->fmap_area_count; i++) {
		const struct fmap_area *area = &policy->fmap_areas[i];

		if (!fmap_name_valid(area) ||
		    area->flags & ~(FMAP_AREA_STATIC | FMAP_AREA_COMPRESSED |
			FMAP_AREA_RO | FMAP_AREA_PRESERVE) ||
		    !area->size ||
		    area->offset > policy->boot_media_size ||
		    area->size > policy->boot_media_size - area->offset)
			return false;
	}
	for (size_t route_index = 0; route_index < policy->region_count;
	     route_index++) {
		const struct lb_capsule_update_region *route =
			&policy->regions[route_index];
		size_t writable_matches = 0;

		for (size_t area_index = 0; area_index < policy->fmap_area_count;
		     area_index++) {
			const struct fmap_area *area = &policy->fmap_areas[area_index];
			bool immutable = area->flags & (FMAP_AREA_STATIC |
				FMAP_AREA_RO | FMAP_AREA_PRESERVE);

			if (!immutable && area->offset == route->flash_offset &&
			    area->size == route->size)
				writable_matches++;
			if (immutable && ranges_overlap(area->offset, area->size,
				route->flash_offset, route->size))
				return false;
		}
		if (writable_matches != 1)
			return false;
	}
	return true;
}

struct broker_control_state {
	uint64_t last_transaction;
	uint64_t grant_transaction;
	uint64_t authenticated_transaction;
	uint64_t authenticated_owner_sequence;
	uint64_t grant_owner_sequence;
	uint64_t success_transaction;
	uint64_t success_owner_sequence;
	uint32_t grant_version;
	uint32_t authenticated_version;
	uint32_t success_version;
	uint32_t success_lowest_supported_version;
	uint8_t grant_digest[CAPSULE_BROKER_DIGEST_SIZE];
	uint8_t authenticated_digest[CAPSULE_BROKER_DIGEST_SIZE];
	uint8_t success_digest[CAPSULE_BROKER_DIGEST_SIZE];
	struct capsule_broker_raw_image grant_raw_image;
	struct capsule_broker_raw_image authenticated_raw_image;
	struct capsule_broker_flash_plan flash_plan;
	bool grant_valid;
	bool authentication_valid;
	bool success_valid;
	bool authentication_in_progress;
	bool flash_plan_valid;
	bool installed;
	bool closed;
};

static struct broker_control_state control_state(void)
{
	struct broker_control_state state = {
		.last_transaction = broker.last_transaction,
		.grant_transaction = broker.grant_transaction,
		.authenticated_transaction = broker.authenticated_transaction,
		.authenticated_owner_sequence = broker.authenticated_owner_sequence,
		.grant_owner_sequence = broker.grant_owner_sequence,
		.success_transaction = broker.success_transaction,
		.success_owner_sequence = broker.success_owner_sequence,
		.grant_version = broker.grant_version,
		.authenticated_version = broker.authenticated_version,
		.success_version = broker.success_version,
		.success_lowest_supported_version =
			broker.success_lowest_supported_version,
		.grant_valid = broker.grant_valid,
		.authentication_valid = broker.authentication_valid,
		.success_valid = broker.success_valid,
		.authentication_in_progress = broker.authentication_in_progress,
		.flash_plan_valid = broker.flash_plan_valid,
		.installed = broker.installed,
		.closed = broker.closed,
		.grant_raw_image = broker.grant_raw_image,
		.authenticated_raw_image = broker.authenticated_raw_image,
		.flash_plan = broker.flash_plan,
	};

	memcpy(state.grant_digest, broker.grant_digest,
		sizeof(state.grant_digest));
	memcpy(state.authenticated_digest, broker.authenticated_digest,
		sizeof(state.authenticated_digest));
	memcpy(state.success_digest, broker.success_digest,
		sizeof(state.success_digest));
	return state;
}

static bool control_state_matches(const struct broker_control_state *expected)
{
	const struct broker_control_state current = control_state();

	return current.last_transaction == expected->last_transaction &&
		current.grant_transaction == expected->grant_transaction &&
		current.authenticated_transaction ==
			expected->authenticated_transaction &&
		current.authenticated_owner_sequence ==
			expected->authenticated_owner_sequence &&
		current.grant_owner_sequence == expected->grant_owner_sequence &&
		current.success_transaction == expected->success_transaction &&
		current.success_owner_sequence == expected->success_owner_sequence &&
		current.grant_version == expected->grant_version &&
		current.authenticated_version == expected->authenticated_version &&
		current.success_version == expected->success_version &&
		current.success_lowest_supported_version ==
			expected->success_lowest_supported_version &&
		!memcmp(current.grant_digest, expected->grant_digest,
			sizeof(current.grant_digest)) &&
		!memcmp(current.authenticated_digest, expected->authenticated_digest,
			sizeof(current.authenticated_digest)) &&
		!memcmp(current.success_digest, expected->success_digest,
			sizeof(current.success_digest)) &&
		!memcmp(&current.grant_raw_image, &expected->grant_raw_image,
			sizeof(current.grant_raw_image)) &&
		!memcmp(&current.authenticated_raw_image,
			&expected->authenticated_raw_image,
			sizeof(current.authenticated_raw_image)) &&
		!memcmp(&current.flash_plan, &expected->flash_plan,
			sizeof(current.flash_plan)) &&
		current.grant_valid == expected->grant_valid &&
		current.authentication_valid == expected->authentication_valid &&
		current.success_valid == expected->success_valid &&
		current.authentication_in_progress ==
			expected->authentication_in_progress &&
		current.flash_plan_valid == expected->flash_plan_valid &&
		current.installed == expected->installed &&
		current.closed == expected->closed;
}

static bool execution_guard(void)
{
	const struct capsule_broker_proofs *proofs = &broker.policy.proofs;
	const struct broker_control_state expected = control_state();
	uint64_t communication = unpack64(broker.policy.endpoint.communication_base);
	uint64_t staging = unpack64(broker.policy.endpoint.staging_base);
	uint64_t staging_size = unpack64(broker.policy.endpoint.staging_size);

	if (!proofs->dma_protected(proofs->context, communication,
		broker.policy.endpoint.communication_size) ||
	    !control_state_matches(&expected))
		return false;
	if (!proofs->dma_protected(proofs->context, staging, staging_size) ||
	    !control_state_matches(&expected))
		return false;
	if (!proofs->smm_spi_owned(proofs->context) ||
	    !control_state_matches(&expected))
		return false;
	if (!proofs->no_raw_flash(proofs->context) ||
	    !control_state_matches(&expected))
		return false;
	return proofs->cpu_rendezvous_active(proofs->context) &&
		control_state_matches(&expected);
}

bool capsule_broker_execution_ready(void)
{
	struct broker_control_state expected;

	if (!broker.installed || broker.closed)
		return false;
	expected = control_state();
	return execution_guard() && control_state_matches(&expected);
}

enum cb_err capsule_broker_policy_install(
	const struct capsule_broker_policy *trusted_policy,
	capsule_broker_protected_storage_fn storage_is_protected, void *context)
{
	struct capsule_broker_policy snapshot;
	uint64_t communication;
	uint64_t staging;
	uint64_t staging_size;
	size_t routes_size;

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
	routes_size = snapshot.region_count * sizeof(snapshot.regions[0]);
	if (snapshot.revision != CAPSULE_BROKER_POLICY_REVISION ||
	    snapshot.size != sizeof(snapshot) ||
	    !snapshot.raw_image_size ||
	    !endpoint_valid(&snapshot.endpoint, snapshot.raw_image_size) ||
	    !snapshot.boot_media_size || !snapshot.smmstore_size ||
	    snapshot.smmstore_offset > snapshot.boot_media_size ||
	    snapshot.smmstore_size > snapshot.boot_media_size -
		snapshot.smmstore_offset || !snapshot.erase_size ||
	    !snapshot.region_count ||
	    snapshot.region_count > CAPSULE_UPDATE_MAX_REGIONS ||
	    !fmap_policy_valid(&snapshot) ||
	    !payload_mm_fmp_layout_valid(&snapshot.owner_layout) ||
	    snapshot.owner_layout.media_size != snapshot.boot_media_size ||
	    snapshot.owner_layout.erase_size != snapshot.erase_size ||
	    snapshot.owner_layout.smmstore.offset != snapshot.smmstore_offset ||
	    snapshot.owner_layout.smmstore.size != snapshot.smmstore_size ||
	    snapshot.owner_layout.route_count != snapshot.region_count ||
	    memcmp(snapshot.owner_layout.route, snapshot.regions, routes_size) ||
	    snapshot.media.size != snapshot.boot_media_size ||
	    snapshot.media.erase_size != snapshot.erase_size ||
	    !snapshot.media.read || !snapshot.media.erase || !snapshot.media.write ||
	    !snapshot.media.sync ||
	    !context_valid(snapshot.media.context,
		snapshot.media_context_size) ||
	    !snapshot.scratch || snapshot.scratch_size != snapshot.erase_size ||
	    !snapshot.write_scratch ||
	    snapshot.write_scratch_size != snapshot.erase_size ||
	    !snapshot.sha256 ||
	    !context_valid(snapshot.sha256_context,
		snapshot.sha256_context_size) ||
	    !snapshot.authenticate ||
	    !context_valid(snapshot.authenticate_context,
		snapshot.authenticate_context_size) ||
	    !proofs_present(&snapshot.proofs) ||
	    !context_valid(snapshot.proofs.context,
		snapshot.proofs.context_size) ||
	    !policy_storage_disjoint(&snapshot, communication, staging,
		staging_size))
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
	if (snapshot.authenticate_context_size) {
		memcpy(broker.authenticate_context, snapshot.authenticate_context,
			snapshot.authenticate_context_size);
		snapshot.authenticate_context = broker.authenticate_context;
	}
	if (snapshot.proofs.context_size) {
		memcpy(broker.proof_context, snapshot.proofs.context,
			snapshot.proofs.context_size);
		snapshot.proofs.context = broker.proof_context;
	}
	if (!storage_is_protected(context, snapshot.scratch,
		snapshot.scratch_size) ||
	    !storage_is_protected(context, snapshot.write_scratch,
		snapshot.write_scratch_size) ||
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
	    ranges_overlap((uintptr_t)snapshot.write_scratch,
		snapshot.write_scratch_size, (uintptr_t)&broker, sizeof(broker)))
		return CB_ERR;
	broker.policy = snapshot;
	memcpy(broker.regions, snapshot.regions,
		snapshot.region_count * sizeof(snapshot.regions[0]));
	broker.installed = true;
	return CB_SUCCESS;
}

static enum cb_err authenticate_intent(
	const struct payload_mm_fmp_capsule_intent *intent_source,
	const struct payload_mm_fmp_owner_record *owner_source)
{
	struct payload_mm_fmp_capsule_intent intent;
	struct payload_mm_fmp_owner_record owner;
	struct payload_mm_fmp_owner_record expected_owner;
	uint8_t context[CAPSULE_BROKER_CONTEXT_SIZE] __aligned(8);
	uint8_t digest[CAPSULE_BROKER_DIGEST_SIZE];
	struct capsule_broker_raw_image callback_raw_image = { 0 };
	struct capsule_broker_raw_image raw_image = { 0 };
	const void *authenticate_context = NULL;
	void *staging;
	enum cb_err callback_status;
	enum cb_err status = CB_ERR;

	if (!broker.installed || broker.closed || broker.authentication_in_progress)
		return CB_ERR;
	if (broker.grant_valid) {
		clear_grant();
		broker.closed = true;
		return CB_ERR;
	}
	if (broker.authentication_valid) {
		clear_authentication();
		broker.closed = true;
		return CB_ERR;
	}
	if (!intent_source || !owner_source ||
	    intent_source != payload_mm_fmp_dispatch_capsule_intent())
		return CB_ERR;
	memcpy(&intent, intent_source, sizeof(intent));
	owner = *owner_source;
	expected_owner = owner;
	if (intent.revision != PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION ||
	    intent.size != sizeof(intent) ||
	    (intent.operation != PAYLOAD_MM_FMP_CAPSULE_CHECK &&
	     intent.operation != PAYLOAD_MM_FMP_CAPSULE_SET) || intent.flags ||
	    !intent.transaction || intent.reserved ||
	    !capsule_broker_intent_matches(intent.broker_generation,
		intent.capsule_size) ||
	    intent.digest_algorithm != PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256 ||
	    intent.digest_size != sizeof(intent.digest))
		return CB_ERR;
	broker.authentication_in_progress = true;
	staging = (void *)(uintptr_t)
		unpack64(broker.policy.endpoint.staging_base);
	if (!execution_guard() || !authentication_state_valid())
		goto out;
	if (broker.policy.sha256(broker.policy.sha256_context, staging,
		(size_t)intent.capsule_size, digest) != CB_SUCCESS ||
	    !authentication_state_valid() || !execution_guard() ||
	    !authentication_state_valid() ||
	    memcmp(digest, intent.digest, sizeof(digest)))
		goto out;
	memset(context, 0, sizeof(context));
	if (broker.policy.authenticate_context_size)
		memcpy(context, broker.authenticate_context,
			broker.policy.authenticate_context_size);
	if (broker.policy.authenticate_context_size)
		authenticate_context = context;
	callback_status = broker.policy.authenticate(authenticate_context, staging,
		(size_t)intent.capsule_size, intent.attempted_version,
		&owner, &callback_raw_image);
	raw_image = callback_raw_image;
	memset(&callback_raw_image, 0, sizeof(callback_raw_image));
	if (callback_status != CB_SUCCESS ||
	    memcmp(&owner, &expected_owner, sizeof(owner)) ||
	    !authentication_state_valid() || !raw_image.size ||
	    raw_image.size != broker.policy.raw_image_size ||
	    raw_image.lowest_supported_version > intent.attempted_version ||
	    raw_image.reserved ||
	    raw_image.offset > intent.capsule_size ||
	    raw_image.size > intent.capsule_size - raw_image.offset)
		goto out;
	if (!execution_guard() || !authentication_state_valid())
		goto out;
	if (broker.policy.sha256(broker.policy.sha256_context, staging,
		(size_t)intent.capsule_size, digest) != CB_SUCCESS ||
	    !authentication_state_valid() || !execution_guard() ||
	    !authentication_state_valid() ||
	    memcmp(digest, intent.digest, sizeof(digest)))
		goto out;
	if (intent.operation == PAYLOAD_MM_FMP_CAPSULE_SET) {
		broker.authenticated_transaction = intent.transaction;
		broker.authenticated_owner_sequence = owner.sequence;
		broker.authenticated_version = intent.attempted_version;
		memcpy(broker.authenticated_digest, intent.digest,
			sizeof(broker.authenticated_digest));
		broker.authenticated_raw_image = raw_image;
		broker.authentication_valid = true;
	}
	status = CB_SUCCESS;
out:
	broker.authentication_in_progress = false;
	memset(&callback_raw_image, 0, sizeof(callback_raw_image));
	memset(&raw_image, 0, sizeof(raw_image));
	if (status != CB_SUCCESS ||
	    intent.operation == PAYLOAD_MM_FMP_CAPSULE_CHECK)
		clear_authentication();
	return status;
}

enum cb_err capsule_broker_authenticate_intent_bound(
	const struct payload_mm_fmp_capsule_intent *intent,
	const struct payload_mm_fmp_owner_record *owner_record)
{
	if (!owner_record || !owner_record->sequence)
		return CB_ERR;
	return authenticate_intent(intent, owner_record);
}

enum cb_err capsule_broker_checkpoint_grant_bound(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version,
	uint64_t authenticated_sequence, uint64_t checkpoint_sequence,
	const uint8_t digest[CAPSULE_BROKER_DIGEST_SIZE])
{
	struct capsule_broker_raw_image raw_image;

	if (!authenticated_sequence || !checkpoint_sequence || !digest ||
	    !broker.installed || broker.closed || broker.grant_valid ||
	    broker.authentication_in_progress || !broker.authentication_valid ||
	    generation != unpack64(broker.policy.endpoint.generation) ||
	    !transaction || transaction <= broker.last_transaction ||
	    transaction != broker.authenticated_transaction ||
	    attempted_version != broker.authenticated_version ||
	    authenticated_sequence != broker.authenticated_owner_sequence ||
	    (checkpoint_sequence != authenticated_sequence &&
	     (authenticated_sequence == UINT64_MAX ||
	      checkpoint_sequence != authenticated_sequence + 1)) ||
	    memcmp(digest, broker.authenticated_digest,
		CAPSULE_BROKER_DIGEST_SIZE)) {
		if (broker.authentication_valid) {
			clear_authentication();
			broker.closed = true;
		}
		if (broker.grant_valid) {
			clear_grant();
			broker.closed = true;
		}
		return CB_ERR;
	}
	raw_image = broker.authenticated_raw_image;
	broker.grant_transaction = transaction;
	broker.grant_owner_sequence = checkpoint_sequence;
	broker.grant_version = attempted_version;
	memcpy(broker.grant_digest, broker.authenticated_digest,
		sizeof(broker.grant_digest));
	broker.grant_raw_image = raw_image;
	clear_authentication();
	broker.grant_valid = true;
	return CB_SUCCESS;
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

static enum cb_err guarded_sync(void *context)
{
	(void)context;
	if (!execution_guard())
		return CB_ERR;
	return broker.policy.media.sync(broker.policy.media.context);
}

static bool guarded_source_valid(void *context, const void *source, size_t size)
{
	uintptr_t staging = (uintptr_t)
		unpack64(broker.policy.endpoint.staging_base);
	uint64_t staging_size =
		unpack64(broker.policy.endpoint.staging_size);
	const struct capsule_broker_raw_image *raw = &broker.flash_plan.raw_image;

	return context == &broker && broker.flash_plan_valid && raw->size == size &&
		raw->offset <= staging_size && raw->size <= staging_size - raw->offset &&
		raw->offset <= UINTPTR_MAX - staging &&
		source == (const void *)(staging + (uintptr_t)raw->offset) &&
		execution_guard();
}

static bool flash_plan_build(
	const struct payload_mm_fmp_capsule_intent *intent,
	struct capsule_broker_flash_plan *plan)
{
	struct capsule_broker_flash_plan candidate = {
		.generation = unpack64(broker.policy.endpoint.generation),
		.transaction = broker.grant_transaction,
		.owner_sequence = broker.grant_owner_sequence,
		.version = broker.grant_version,
		.region_count = broker.policy.region_count,
		.raw_image = broker.grant_raw_image,
	};

	if (!intent || !plan || !broker.grant_valid ||
	    candidate.generation != intent->broker_generation ||
	    candidate.transaction != intent->transaction ||
	    candidate.version != intent->attempted_version ||
	    !candidate.owner_sequence || !candidate.raw_image.size ||
	    memcmp(broker.grant_digest, intent->digest,
		sizeof(candidate.capsule_digest)))
		return false;
	memcpy(candidate.capsule_digest, broker.grant_digest,
		sizeof(candidate.capsule_digest));
	memcpy(candidate.regions, broker.regions,
		candidate.region_count * sizeof(candidate.regions[0]));
	*plan = candidate;
	return true;
}

static enum cb_err apply_capsule(
	const struct payload_mm_fmp_capsule_intent *intent)
{
	struct capsule_update_plan plan;
	struct capsule_media_policy media_policy;
	struct capsule_media_backend media;
	uint8_t digest[CAPSULE_BROKER_DIGEST_SIZE];
	struct capsule_broker_flash_plan sealed_plan;
	uintptr_t staging = (uintptr_t)
		unpack64(broker.policy.endpoint.staging_base);
	enum cb_err status = CB_ERR;

	if (!flash_plan_build(intent, &sealed_plan)) {
		broker.closed = true;
		clear_grant();
		clear_authentication();
		clear_flash_plan();
		return CB_ERR;
	}
	broker.flash_plan = sealed_plan;
	broker.flash_plan_valid = true;
	broker.closed = true;
	clear_grant();
	clear_authentication();
	if (!execution_guard() ||
	    broker.policy.sha256(broker.policy.sha256_context,
		(void *)staging, (size_t)intent->capsule_size, digest) != CB_SUCCESS ||
	    !execution_guard() || memcmp(digest, intent->digest, sizeof(digest)))
		goto out;
	plan = (struct capsule_update_plan) {
		.image = (const void *)(staging +
			(uintptr_t)broker.flash_plan.raw_image.offset),
		.image_bytes = (size_t)broker.flash_plan.raw_image.size,
		.regions = broker.flash_plan.regions,
		.region_count = broker.flash_plan.region_count,
	};
	media_policy = (struct capsule_media_policy) {
		.media_size = broker.policy.boot_media_size,
		.erase_size = broker.policy.erase_size,
		.smmstore_offset = broker.policy.smmstore_offset,
		.smmstore_size = broker.policy.smmstore_size,
		.regions = broker.regions,
		.region_count = broker.policy.region_count,
		.owner_layout = &broker.policy.owner_layout,
	};
	media = (struct capsule_media_backend) {
		.context = &broker,
		.size = broker.policy.media.size,
		.erase_size = broker.policy.media.erase_size,
		.read = guarded_read,
		.erase = guarded_erase,
		.write = guarded_write,
		.sync = guarded_sync,
		.source_valid = guarded_source_valid,
	};
	status = capsule_apply_policy_verified(&plan, &media_policy, &media,
		broker.policy.write_scratch, broker.policy.scratch,
		broker.policy.scratch_size);
	if (status == CB_SUCCESS) {
		broker.success_transaction = broker.flash_plan.transaction;
		broker.success_owner_sequence = broker.flash_plan.owner_sequence;
		broker.success_version = broker.flash_plan.version;
		broker.success_lowest_supported_version =
			broker.flash_plan.raw_image.lowest_supported_version;
		memcpy(broker.success_digest, broker.flash_plan.capsule_digest,
			sizeof(broker.success_digest));
		broker.success_valid = true;
	}
out:
	memset(broker.policy.write_scratch, 0, broker.policy.write_scratch_size);
	memset(broker.policy.scratch, 0, broker.policy.scratch_size);
	clear_flash_plan();
	return status;
}

enum cb_err capsule_broker_apply_intent(
	const struct payload_mm_fmp_capsule_intent *intent_source)
{
	struct payload_mm_fmp_capsule_intent intent;
	bool consume = broker.grant_valid;

	if (!intent_source ||
	    intent_source != payload_mm_fmp_dispatch_capsule_intent()) {
		if (consume)
			capsule_broker_close_for_s3();
		return CB_ERR;
	}
	memcpy(&intent, intent_source, sizeof(intent));
	if (intent.revision != PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION ||
	    intent.size != sizeof(intent) ||
	    intent.operation != PAYLOAD_MM_FMP_CAPSULE_SET || intent.flags ||
	    intent.broker_generation !=
		unpack64(broker.policy.endpoint.generation) ||
	    !intent.transaction ||
	    !intent.capsule_size || intent.capsule_size >
		unpack64(broker.policy.endpoint.staging_size) ||
	    intent.digest_algorithm != PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256 ||
	    intent.digest_size != sizeof(intent.digest) || intent.reserved ||
	    !broker.grant_valid || !broker.grant_owner_sequence ||
	    !broker.grant_raw_image.size ||
	    broker.grant_raw_image.size != broker.policy.raw_image_size ||
	    broker.grant_raw_image.offset > intent.capsule_size ||
	    broker.grant_raw_image.size >
		intent.capsule_size - broker.grant_raw_image.offset ||
	    broker.grant_raw_image.reserved ||
	    intent.transaction != broker.grant_transaction ||
	    intent.attempted_version != broker.grant_version ||
	    memcmp(intent.digest, broker.grant_digest, sizeof(intent.digest))) {
		if (consume)
			capsule_broker_close_for_s3();
		return CB_ERR;
	}
	if (intent.transaction <= broker.last_transaction) {
		capsule_broker_close_for_s3();
		return CB_ERR;
	}
	broker.last_transaction = intent.transaction;
	return apply_capsule(&intent);
}

enum cb_err capsule_broker_success_claim_bound(uint64_t generation,
	uint64_t transaction, uint64_t checkpoint_sequence,
	const uint8_t digest[CAPSULE_BROKER_DIGEST_SIZE],
	struct capsule_broker_success *success)
{
	struct capsule_broker_success result;

	if (!success || (uintptr_t)success % _Alignof(*success) || !digest ||
	    ranges_overlap((uintptr_t)success, sizeof(*success),
		(uintptr_t)&broker, sizeof(broker)) ||
	    ranges_overlap((uintptr_t)success, sizeof(*success),
		(uintptr_t)digest, CAPSULE_BROKER_DIGEST_SIZE) ||
	    ranges_overlap((uintptr_t)success, sizeof(*success),
		unpack64(broker.policy.endpoint.communication_base),
		broker.policy.endpoint.communication_size) ||
	    ranges_overlap((uintptr_t)success, sizeof(*success),
		unpack64(broker.policy.endpoint.staging_base),
		unpack64(broker.policy.endpoint.staging_size)) ||
	    ranges_overlap((uintptr_t)success, sizeof(*success),
		(uintptr_t)broker.policy.scratch, broker.policy.scratch_size) ||
	    ranges_overlap((uintptr_t)success, sizeof(*success),
		(uintptr_t)broker.policy.write_scratch,
		broker.policy.write_scratch_size))
		return CB_ERR;
	memset(success, 0, sizeof(*success));
	if (!broker.installed || !broker.closed || !broker.success_valid ||
	    generation != unpack64(broker.policy.endpoint.generation) ||
	    !transaction || transaction != broker.success_transaction ||
	    !checkpoint_sequence ||
	    checkpoint_sequence != broker.success_owner_sequence ||
	    memcmp(digest, broker.success_digest, sizeof(broker.success_digest))) {
		clear_success();
		return CB_ERR;
	}
	result = (struct capsule_broker_success) {
		.version = broker.success_version,
		.lowest_supported_version =
			broker.success_lowest_supported_version,
	};
	clear_success();
	*success = result;
	return CB_SUCCESS;
}

void capsule_broker_close_for_s3(void)
{
	broker.closed = true;
	clear_grant();
	clear_authentication();
	clear_success();
	clear_flash_plan();
}
