/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_CAPSULE_BROKER_INTERNAL_H
#define LIB_CAPSULE_BROKER_INTERNAL_H

#include <boot/capsule_broker.h>
#include <boot/payload_mm_authvar.h>
#include "capsule_update_internal.h"

typedef bool capsule_broker_range_proof_fn(void *context, uint64_t base,
	uint64_t size);
typedef bool capsule_broker_state_proof_fn(void *context);
typedef enum cb_err capsule_broker_sha256_fn(void *context, const void *data,
	size_t size, uint8_t digest[CAPSULE_BROKER_DIGEST_SIZE]);
typedef enum cb_err capsule_broker_authenticate_fn(const void *context,
	const void *image, size_t image_size, uint32_t attempted_version);

struct capsule_broker_proofs {
	capsule_broker_range_proof_fn *communication_reserved;
	capsule_broker_range_proof_fn *staging_reserved;
	capsule_broker_range_proof_fn *dma_protected;
	capsule_broker_state_proof_fn *smm_spi_owned;
	capsule_broker_state_proof_fn *no_raw_flash;
	capsule_broker_state_proof_fn *cpu_rendezvous_active;
	void *context;
	size_t context_size;
};

#define CAPSULE_BROKER_CONTEXT_SIZE 128U

struct capsule_broker_policy {
	uint32_t revision;
	uint32_t size;
	struct lb_capsule_broker_endpoint endpoint;
	uint64_t image_size;
	uint64_t boot_media_size;
	uint64_t smmstore_offset;
	uint64_t smmstore_size;
	uint32_t erase_size;
	uint32_t region_count;
	struct lb_capsule_update_region regions[CAPSULE_UPDATE_MAX_REGIONS];
	struct capsule_media_backend media;
	size_t media_context_size;
	void *scratch;
	size_t scratch_size;
	capsule_broker_sha256_fn *sha256;
	void *sha256_context;
	size_t sha256_context_size;
	capsule_broker_authenticate_fn *authenticate;
	const void *authenticate_context;
	size_t authenticate_context_size;
	struct capsule_broker_proofs proofs;
};

#define CAPSULE_BROKER_POLICY_REVISION 1U

typedef bool capsule_broker_protected_storage_fn(void *context,
	const void *storage, size_t size);

enum cb_err capsule_broker_policy_install(
	const struct capsule_broker_policy *trusted_policy,
	capsule_broker_protected_storage_fn storage_is_protected, void *context);
bool capsule_broker_generation_matches(uint64_t generation);
bool capsule_broker_intent_matches(uint64_t generation, uint64_t image_size);
bool capsule_broker_buffer_available(const void *buffer, size_t size);
enum cb_err capsule_broker_authenticate_intent(
	const struct payload_mm_fmp_capsule_intent *intent);

/*
 * Called only by the protected variable owner after a matching SET was
 * authenticated and its durable failure checkpoint committed.
 */
enum cb_err capsule_broker_checkpoint_grant(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version);

enum cb_err capsule_broker_handle(void);
void capsule_broker_close_for_s3(void);

#endif
