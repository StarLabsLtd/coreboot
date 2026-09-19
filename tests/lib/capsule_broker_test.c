/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_broker.h>
#include "../../src/lib/capsule_broker_internal.h"
#include "../../src/lib/payload_mm_fmp_owner_internal.h"
#include <string.h>

#define GENERATION 0x1122334455667788ULL
#define IMAGE_SIZE 0x2000U
#define MEDIA_SIZE 0x8000U
#define ERASE_SIZE 0x1000U

struct media_context {
	uint8_t *bytes;
	size_t *reads;
	size_t *erases;
	size_t *writes;
	bool corrupt_readback;
	bool fail_erase;
	bool fail_write;
	bool fail_read;
};

struct sha256_context {
	bool drop_guard;
};

struct authenticate_context {
	uint32_t magic;
	uint32_t expected_version;
	bool allow;
};

struct proof_context {
	uint64_t communication;
	uint64_t communication_size;
	uint64_t staging;
	uint64_t staging_size;
	bool communication_reserved;
	bool staging_reserved;
	bool dma_protected;
	bool smm_spi_owned;
	bool no_raw_flash;
	bool rendezvous;
};

static bool runtime_dma_guard;
static struct proof_context *install_mutation_target;
static struct authenticate_context *install_auth_mutation_target;
static size_t authenticate_calls;
static bool authenticate_mutates_image;
static bool authenticate_mutates_context;
static bool authenticate_mutates_owner;
static bool authenticate_drops_guard;
static const struct payload_mm_fmp_capsule_intent *staged_intent;
static struct payload_mm_fmp_capsule_intent *callback_intent;
static bool authenticate_reenters;
static bool authenticate_attempts_grant;
static bool authenticate_attempts_apply;
static bool authenticate_attempts_close;
static bool authenticate_mutates_intent;
static enum cb_err nested_authenticate_status;
static enum cb_err nested_grant_status;
static enum cb_err nested_apply_status;
static struct payload_mm_fmp_owner_record owner_record;
enum callback_attack {
	CALLBACK_ATTACK_NONE,
	CALLBACK_ATTACK_CLOSE,
	CALLBACK_ATTACK_REENTER_GRANT,
};
enum proof_callback {
	PROOF_DMA_COMMUNICATION = 1,
	PROOF_DMA_STAGING,
	PROOF_SPI,
	PROOF_NO_RAW,
	PROOF_RENDEZVOUS,
	PROOF_COUNT,
};
static enum callback_attack callback_attack;
static enum proof_callback proof_attack_target;
static size_t proof_calls[PROOF_COUNT];
static size_t sha256_calls;
static size_t sha256_attack_call;

const struct payload_mm_fmp_capsule_intent *
payload_mm_fmp_dispatch_capsule_intent(void)
{
	return staged_intent;
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static void callback_attack_once(void)
{
	const enum callback_attack attack = callback_attack;

	callback_attack = CALLBACK_ATTACK_NONE;
	if (attack == CALLBACK_ATTACK_CLOSE) {
		capsule_broker_close_for_s3();
		return;
	}
	if (attack == CALLBACK_ATTACK_REENTER_GRANT) {
		nested_authenticate_status =
			capsule_broker_authenticate_intent_bound(staged_intent,
				&owner_record);
		nested_grant_status = capsule_broker_checkpoint_grant_bound(
			callback_intent->broker_generation,
			callback_intent->transaction,
			callback_intent->attempted_version, owner_record.sequence,
			owner_record.sequence + 1, callback_intent->digest);
	}
}

static void proof_callback_enter(enum proof_callback callback)
{
	proof_calls[callback]++;
	if (callback == proof_attack_target &&
	    callback_attack != CALLBACK_ATTACK_NONE)
		callback_attack_once();
}

struct fixture {
	uint8_t communication[sizeof(struct capsule_broker_message)] __aligned(8);
	struct payload_mm_fmp_capsule_intent capsule;
	uint8_t staging[IMAGE_SIZE] __aligned(8);
	uint8_t media[MEDIA_SIZE];
	uint8_t original[MEDIA_SIZE];
	uint8_t scratch[ERASE_SIZE] __aligned(8);
	struct media_context media_context;
	struct sha256_context sha256_context;
	struct authenticate_context authenticate_context;
	struct proof_context proof_context;
	struct capsule_broker_policy policy;
	struct lb_capsule_handoff handoff;
	bool storage_protected;
	size_t reads;
	size_t erases;
	size_t writes;
};

static bool storage_protected(void *context, const void *storage, size_t size)
{
	struct fixture *fixture = context;

	(void)storage;
	(void)size;
	return fixture->storage_protected;
}

static bool communication_reserved(void *context, uint64_t base, uint64_t size)
{
	struct proof_context *proof = context;
	bool valid = proof->communication_reserved &&
		base == proof->communication && size == proof->communication_size;

	if (install_mutation_target != NULL) {
		install_mutation_target->communication += 8;
		install_mutation_target = NULL;
	}
	if (install_auth_mutation_target != NULL) {
		install_auth_mutation_target->allow = false;
		install_auth_mutation_target = NULL;
	}
	return valid;
}

static bool staging_reserved(void *context, uint64_t base, uint64_t size)
{
	struct proof_context *proof = context;

	return proof->staging_reserved && base == proof->staging &&
		size == proof->staging_size;
}

static bool dma_protected(void *context, uint64_t base, uint64_t size)
{
	struct proof_context *proof = context;
	bool range_matches;
	enum proof_callback callback;

	callback = base == proof->communication ? PROOF_DMA_COMMUNICATION :
		PROOF_DMA_STAGING;
	proof_callback_enter(callback);
	range_matches = (base == proof->communication &&
		size == proof->communication_size) ||
		(base == proof->staging && size == proof->staging_size);
	return proof->dma_protected && runtime_dma_guard && range_matches;
}

static bool smm_spi_owned(void *context)
{
	proof_callback_enter(PROOF_SPI);
	return ((struct proof_context *)context)->smm_spi_owned;
}

static bool no_raw_flash(void *context)
{
	proof_callback_enter(PROOF_NO_RAW);
	return ((struct proof_context *)context)->no_raw_flash;
}

static bool rendezvous(void *context)
{
	proof_callback_enter(PROOF_RENDEZVOUS);
	return ((struct proof_context *)context)->rendezvous;
}

static void calculate_digest(const void *data, size_t size,
	uint8_t digest[CAPSULE_BROKER_DIGEST_SIZE])
{
	const uint8_t *bytes = data;

	memset(digest, 0, CAPSULE_BROKER_DIGEST_SIZE);
	for (size_t i = 0; i < size; i++)
		digest[i % CAPSULE_BROKER_DIGEST_SIZE] ^= bytes[i] + (uint8_t)i;
}

static enum cb_err sha256(void *context, const void *data, size_t size,
	uint8_t digest[CAPSULE_BROKER_DIGEST_SIZE])
{
	struct sha256_context *sha_context = context;

	sha256_calls++;
	if (sha256_calls == sha256_attack_call &&
	    callback_attack != CALLBACK_ATTACK_NONE)
		callback_attack_once();
	calculate_digest(data, size, digest);
	if (sha_context->drop_guard)
		runtime_dma_guard = false;
	return CB_SUCCESS;
}

static enum cb_err authenticate(const void *opaque, const void *image,
	size_t image_size, uint32_t attempted_version,
	const struct payload_mm_fmp_owner_record *owner)
{
	const struct authenticate_context *context = opaque;
	bool allowed;

	authenticate_calls++;
	if (!context || context->magic != 0x41555448U ||
	    context->expected_version != attempted_version ||
	    image_size != IMAGE_SIZE || ((const uint8_t *)image)[0] != 0)
		return CB_ERR;
	assert(!memcmp(owner, &owner_record, sizeof(owner_record)));
	allowed = context->allow;
	if (authenticate_mutates_context)
		((struct authenticate_context *)context)->allow = false;
	if (authenticate_mutates_owner)
		((struct payload_mm_fmp_owner_record *)owner)->data[0] ^= 1;
	if (authenticate_mutates_image)
		((uint8_t *)image)[0] ^= 1;
	if (authenticate_drops_guard)
		runtime_dma_guard = false;
	if (authenticate_reenters)
		nested_authenticate_status =
			capsule_broker_authenticate_intent_bound(staged_intent,
				&owner_record);
	if (authenticate_attempts_grant)
		nested_grant_status = capsule_broker_checkpoint_grant_bound(
			callback_intent->broker_generation,
			callback_intent->transaction,
			callback_intent->attempted_version, owner_record.sequence,
			owner_record.sequence + 1, callback_intent->digest);
	if (authenticate_attempts_apply)
		nested_apply_status = capsule_broker_apply_intent(staged_intent);
	if (authenticate_mutates_intent) {
		callback_intent->transaction++;
		callback_intent->digest[0] ^= 1;
	}
	if (authenticate_attempts_close)
		capsule_broker_close_for_s3();
	return allowed ? CB_SUCCESS : CB_ERR;
}

static enum cb_err media_read(void *context, u64 offset, void *data, size_t size)
{
	struct media_context *media = context;

	(*media->reads)++;
	if (media->fail_read)
		return CB_ERR;
	memcpy(data, &media->bytes[offset], size);
	if (media->corrupt_readback)
		((uint8_t *)data)[0] ^= 1;
	return CB_SUCCESS;
}

static enum cb_err media_erase(void *context, u64 offset, size_t size)
{
	struct media_context *media = context;

	(*media->erases)++;
	if (media->fail_erase)
		return CB_ERR;
	memset(&media->bytes[offset], 0xff, size);
	return CB_SUCCESS;
}

static enum cb_err media_write(void *context, u64 offset, const void *data,
	size_t size)
{
	struct media_context *media = context;

	(*media->writes)++;
	if (media->fail_write)
		return CB_ERR;
	memcpy(&media->bytes[offset], data, size);
	return CB_SUCCESS;
}

static void initialize(struct fixture *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	memset(fixture->media, 0x5a, sizeof(fixture->media));
	memcpy(fixture->original, fixture->media, sizeof(fixture->media));
	for (size_t i = 0; i < sizeof(fixture->staging); i++)
		fixture->staging[i] = (uint8_t)i;
	fixture->storage_protected = true;
	runtime_dma_guard = true;
	authenticate_calls = 0;
	authenticate_mutates_image = false;
	authenticate_mutates_context = false;
	authenticate_mutates_owner = false;
	authenticate_drops_guard = false;
	staged_intent = NULL;
	callback_intent = NULL;
	authenticate_reenters = false;
	authenticate_attempts_grant = false;
	authenticate_attempts_apply = false;
	authenticate_attempts_close = false;
	authenticate_mutates_intent = false;
	nested_authenticate_status = CB_SUCCESS;
	nested_grant_status = CB_SUCCESS;
	nested_apply_status = CB_SUCCESS;
	owner_record = (struct payload_mm_fmp_owner_record) {
		.sequence = 19,
		.present = 1,
		.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		.data_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE,
	};
	callback_attack = CALLBACK_ATTACK_NONE;
	proof_attack_target = 0;
	memset(proof_calls, 0, sizeof(proof_calls));
	sha256_calls = 0;
	sha256_attack_call = 0;
	install_auth_mutation_target = NULL;
	fixture->media_context = (struct media_context) {
		.bytes = fixture->media,
		.reads = &fixture->reads,
		.erases = &fixture->erases,
		.writes = &fixture->writes,
	};
	fixture->sha256_context = (struct sha256_context) { 0 };
	fixture->authenticate_context = (struct authenticate_context) {
		.magic = 0x41555448U,
		.expected_version = 11,
		.allow = true,
	};
	fixture->proof_context = (struct proof_context) {
		.communication = (uintptr_t)fixture->communication,
		.communication_size = sizeof(fixture->communication),
		.staging = (uintptr_t)fixture->staging,
		.staging_size = sizeof(fixture->staging),
		.communication_reserved = true,
		.staging_reserved = true,
		.dma_protected = true,
		.smm_spi_owned = true,
		.no_raw_flash = true,
		.rendezvous = true,
	};
	fixture->handoff = (struct lb_capsule_handoff) {
		.tag = LB_TAG_CAPSULE_HANDOFF,
		.size = sizeof(fixture->handoff),
		.revision = LB_CAPSULE_HANDOFF_REVISION,
		.header_size = sizeof(fixture->handoff),
		.image_size = IMAGE_SIZE,
	};
	fixture->policy = (struct capsule_broker_policy) {
		.revision = CAPSULE_BROKER_POLICY_REVISION,
		.size = sizeof(fixture->policy),
		.endpoint = {
			.tag = LB_TAG_CAPSULE_BROKER_ENDPOINT,
			.size = sizeof(struct lb_capsule_broker_endpoint),
			.revision = LB_CAPSULE_BROKER_ENDPOINT_REVISION,
			.header_size = sizeof(struct lb_capsule_broker_endpoint),
			.flags = LB_CAPSULE_ENDPOINT_REQUIRED_FLAGS,
			.generation = GENERATION,
			.communication_base = (uintptr_t)fixture->communication,
			.communication_size = sizeof(fixture->communication),
			.message_size = sizeof(fixture->communication),
			.staging_base = (uintptr_t)fixture->staging,
			.staging_size = sizeof(fixture->staging),
			.transport = LB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8,
			.trigger_width = 1,
			.trigger_address = 0xb2,
			.trigger_value = 0x91,
		},
		.image_size = IMAGE_SIZE,
		.boot_media_size = MEDIA_SIZE,
		.smmstore_offset = 0x6000,
		.smmstore_size = 0x1000,
		.erase_size = ERASE_SIZE,
		.region_count = 1,
		.regions = {{
			.image_offset = 0,
			.flash_offset = 0x1000,
			.size = IMAGE_SIZE,
			.flags = LB_CAPSULE_REGION_BIOS,
		}},
		.media = {
			.context = &fixture->media_context,
			.size = MEDIA_SIZE,
			.erase_size = ERASE_SIZE,
			.read = media_read,
			.erase = media_erase,
			.write = media_write,
		},
		.media_context_size = sizeof(fixture->media_context),
		.scratch = fixture->scratch,
		.scratch_size = sizeof(fixture->scratch),
		.sha256 = sha256,
		.sha256_context = &fixture->sha256_context,
		.sha256_context_size = sizeof(fixture->sha256_context),
		.authenticate = authenticate,
		.authenticate_context = &fixture->authenticate_context,
		.authenticate_context_size = sizeof(fixture->authenticate_context),
		.proofs = {
			.communication_reserved = communication_reserved,
			.staging_reserved = staging_reserved,
			.dma_protected = dma_protected,
			.smm_spi_owned = smm_spi_owned,
			.no_raw_flash = no_raw_flash,
			.cpu_rendezvous_active = rendezvous,
			.context = &fixture->proof_context,
			.context_size = sizeof(fixture->proof_context),
		},
	};
}

static struct payload_mm_fmp_capsule_intent intent(const struct fixture *fixture,
	uint32_t operation, uint64_t transaction, uint32_t attempted_version)
{
	struct payload_mm_fmp_capsule_intent capsule = {
		.revision = PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION,
		.size = sizeof(capsule),
		.operation = operation,
		.broker_generation = GENERATION,
		.transaction = transaction,
		.image_size = IMAGE_SIZE,
		.digest_algorithm = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256,
		.digest_size = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE,
		.attempted_version = attempted_version,
	};

	calculate_digest(fixture->staging, sizeof(fixture->staging),
		capsule.digest);
	return capsule;
}

static enum cb_err authenticate_intent(
	struct payload_mm_fmp_capsule_intent *capsule)
{
	enum cb_err status;

	staged_intent = capsule;
	callback_intent = capsule;
	status = capsule_broker_authenticate_intent_bound(staged_intent,
		&owner_record);
	return status;
}

static enum cb_err checkpoint_grant(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version)
{
	assert(staged_intent != NULL);
	return capsule_broker_checkpoint_grant_bound(generation, transaction,
		attempted_version, owner_record.sequence,
		owner_record.sequence + 1, staged_intent->digest);
}

static enum cb_err apply_staged(void)
{
	return capsule_broker_apply_intent(staged_intent);
}

static void authenticate_set(struct fixture *fixture, uint64_t transaction,
	uint32_t attempted_version)
{
	fixture->capsule = intent(fixture,
		PAYLOAD_MM_FMP_CAPSULE_SET, transaction, attempted_version);

	assert(authenticate_intent(&fixture->capsule) == CB_SUCCESS);
}

static void install(struct fixture *fixture)
{
	assert(capsule_broker_endpoint_validate(&fixture->policy.endpoint,
		&fixture->handoff) == CB_SUCCESS);
	assert(capsule_broker_policy_install(&fixture->policy, storage_protected,
		fixture) == CB_SUCCESS);
}

static void happy(void)
{
	struct fixture fixture;

	initialize(&fixture);
	install(&fixture);
	authenticate_set(&fixture, 1, 11);
	assert(checkpoint_grant(GENERATION, 1, 11) == CB_SUCCESS);
	assert(apply_staged() == CB_SUCCESS);
	assert(fixture.erases == 2 && fixture.writes == 2 && fixture.reads == 2);
	assert(!memcmp(&fixture.media[0x1000], fixture.staging, IMAGE_SIZE));
	assert(!memcmp(fixture.media, fixture.original, 0x1000));
	assert(!memcmp(&fixture.media[0x3000], &fixture.original[0x3000],
		MEDIA_SIZE - 0x3000));
	assert(apply_staged() == CB_ERR);
}

static void generation_match_case(void)
{
	struct fixture fixture;

	initialize(&fixture);
	assert(!capsule_broker_generation_matches(GENERATION));
	assert(!capsule_broker_intent_matches(GENERATION, IMAGE_SIZE));
	install(&fixture);
	assert(capsule_broker_generation_matches(GENERATION));
	assert(capsule_broker_intent_matches(GENERATION, IMAGE_SIZE));
	assert(!capsule_broker_intent_matches(GENERATION, IMAGE_SIZE - 1));
	assert(!capsule_broker_intent_matches(GENERATION + 1, IMAGE_SIZE));
	assert(!capsule_broker_generation_matches(0));
	assert(!capsule_broker_generation_matches(GENERATION + 1));
	fixture.policy.endpoint.generation++;
	assert(capsule_broker_generation_matches(GENERATION));
	capsule_broker_close_for_s3();
	assert(!capsule_broker_generation_matches(GENERATION));
	assert(!capsule_broker_intent_matches(GENERATION, IMAGE_SIZE));
}

static void bound_transaction_case(const char *mode)
{
	struct fixture fixture;
	struct payload_mm_fmp_capsule_intent capsule;
	uint8_t wrong_digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];

	initialize(&fixture);
	install(&fixture);
	capsule = intent(&fixture, PAYLOAD_MM_FMP_CAPSULE_SET, 1, 11);
	staged_intent = &capsule;
	callback_intent = &capsule;
	assert(capsule_broker_authenticate_intent_bound(&capsule, &owner_record) ==
		CB_SUCCESS);
	memcpy(wrong_digest, capsule.digest, sizeof(wrong_digest));
	wrong_digest[0] ^= 1;
	if (!strcmp(mode, "sequence")) {
		assert(capsule_broker_checkpoint_grant_bound(GENERATION, 1, 11, 20,
			21, capsule.digest) == CB_ERR);
		assert(capsule_broker_checkpoint_grant_bound(GENERATION, 1, 11, 19,
			20, capsule.digest) == CB_SUCCESS);
	} else if (!strcmp(mode, "digest")) {
		assert(capsule_broker_checkpoint_grant_bound(GENERATION, 1, 11, 19,
			20, wrong_digest) == CB_ERR);
		assert(capsule_broker_checkpoint_grant_bound(GENERATION, 1, 11, 19,
			20, capsule.digest) == CB_SUCCESS);
	} else {
		assert(capsule_broker_checkpoint_grant_bound(GENERATION, 1, 11, 19,
			20, capsule.digest) == CB_SUCCESS);
	}
	if (!strcmp(mode, "unstaged")) {
		staged_intent = NULL;
		assert(capsule_broker_apply_intent(&capsule) == CB_ERR);
		assert(!fixture.reads && !fixture.erases && !fixture.writes);
		return;
	}
	if (!strcmp(mode, "mutation"))
		capsule.digest[0] ^= 1;
	if (!strcmp(mode, "mutation")) {
		assert(capsule_broker_apply_intent(&capsule) == CB_ERR);
		assert(!fixture.reads && !fixture.erases && !fixture.writes);
	} else {
		assert(capsule_broker_apply_intent(&capsule) == CB_SUCCESS);
		assert(fixture.erases == 2 && fixture.writes == 2 &&
			fixture.reads == 2);
		assert(!memcmp(&fixture.media[0x1000], fixture.staging, IMAGE_SIZE));
	}
	staged_intent = NULL;
	callback_intent = NULL;
}

static void endpoint_mutations(void)
{
	struct fixture fixture;
	struct lb_capsule_broker_endpoint endpoint;

	initialize(&fixture);
#define REJECT(member, value) do { \
	endpoint = fixture.policy.endpoint; \
	endpoint.member = (value); \
	assert(capsule_broker_endpoint_validate(&endpoint, &fixture.handoff) == \
		CB_ERR); \
} while (0)
	REJECT(tag, 0);
	REJECT(size, 79);
	REJECT(revision, 2);
	REJECT(header_size, 76);
	REJECT(flags, LB_CAPSULE_ENDPOINT_REQUIRED_FLAGS ^ 1U);
	REJECT(generation, 0);
	REJECT(communication_base, 0);
	REJECT(communication_base, (uintptr_t)fixture.communication + 1);
	REJECT(communication_size, 95);
	REJECT(message_size, 88);
	REJECT(staging_base, 0);
	REJECT(staging_base, (uintptr_t)fixture.staging + 1);
	REJECT(staging_size, IMAGE_SIZE - 1);
	REJECT(transport, 2);
	REJECT(trigger_width, 4);
	REJECT(trigger_address, 0);
	REJECT(trigger_address, 0x10000);
	REJECT(trigger_value, 0);
	REJECT(trigger_value, 0x100);
#undef REJECT
	endpoint = fixture.policy.endpoint;
	endpoint.reserved[2] = 1;
	assert(capsule_broker_endpoint_validate(&endpoint, &fixture.handoff) ==
		CB_ERR);
	endpoint = fixture.policy.endpoint;
	endpoint.staging_base = endpoint.communication_base;
	assert(capsule_broker_endpoint_validate(&endpoint, &fixture.handoff) ==
		CB_ERR);
	fixture.handoff.image_size = IMAGE_SIZE + 1;
	assert(capsule_broker_endpoint_validate(&fixture.policy.endpoint,
		&fixture.handoff) == CB_ERR);
	fixture.handoff.image_size = IMAGE_SIZE;
	fixture.handoff.tag = 0;
	assert(capsule_broker_endpoint_validate(&fixture.policy.endpoint,
		&fixture.handoff) == CB_ERR);
}

static void invalid_install(const char *mode)
{
	struct fixture fixture;

	initialize(&fixture);
	if (!strcmp(mode, "storage"))
		fixture.storage_protected = false;
	else if (!strcmp(mode, "communication"))
		fixture.proof_context.communication_reserved = false;
	else if (!strcmp(mode, "staging"))
		fixture.proof_context.staging_reserved = false;
	else if (!strcmp(mode, "dma"))
		fixture.proof_context.dma_protected = false;
	else if (!strcmp(mode, "spi"))
		fixture.proof_context.smm_spi_owned = false;
	else if (!strcmp(mode, "raw"))
		fixture.proof_context.no_raw_flash = false;
	else if (!strcmp(mode, "rendezvous"))
		fixture.proof_context.rendezvous = false;
	else if (!strcmp(mode, "scratch"))
		fixture.policy.scratch_size--;
	else if (!strcmp(mode, "geometry"))
		fixture.policy.media.erase_size *= 2;
	else if (!strcmp(mode, "image-size"))
		fixture.policy.image_size--;
	else if (!strcmp(mode, "scratch-communication"))
		fixture.policy.scratch = fixture.communication;
	else if (!strcmp(mode, "scratch-staging"))
		fixture.policy.scratch = fixture.staging;
	else if (!strcmp(mode, "region-count"))
		fixture.policy.region_count = 0;
	else if (!strcmp(mode, "missing-hash"))
		fixture.policy.sha256 = NULL;
	else if (!strcmp(mode, "missing-authenticate"))
		fixture.policy.authenticate = NULL;
	else if (!strcmp(mode, "media-context-null"))
		fixture.policy.media.context = NULL;
	else if (!strcmp(mode, "media-context-large"))
		fixture.policy.media_context_size = CAPSULE_BROKER_CONTEXT_SIZE + 1U;
	else if (!strcmp(mode, "sha-context-null"))
		fixture.policy.sha256_context = NULL;
	else if (!strcmp(mode, "sha-context-large"))
		fixture.policy.sha256_context_size = CAPSULE_BROKER_CONTEXT_SIZE + 1U;
	else if (!strcmp(mode, "authenticate-context-null"))
		fixture.policy.authenticate_context = NULL;
	else if (!strcmp(mode, "authenticate-context-large"))
		fixture.policy.authenticate_context_size =
			CAPSULE_BROKER_CONTEXT_SIZE + 1U;
	else if (!strcmp(mode, "proof-context-null"))
		fixture.policy.proofs.context = NULL;
	else if (!strcmp(mode, "proof-context-large"))
		fixture.policy.proofs.context_size = CAPSULE_BROKER_CONTEXT_SIZE + 1U;
	else
		assert(0);
	assert(capsule_broker_policy_install(&fixture.policy, storage_protected,
		&fixture) == CB_ERR);
	assert(capsule_broker_policy_install(&fixture.policy, storage_protected,
		&fixture) == CB_ERR);
	assert(!fixture.reads && !fixture.erases && !fixture.writes);
}

static void rejected_apply(const char *mode)
{
	struct fixture fixture;

	initialize(&fixture);
	if (!strcmp(mode, "preflight"))
		fixture.policy.regions[0].flash_offset = 0x1800;
	else if (!strcmp(mode, "preflight-range"))
		fixture.policy.regions[0].size = MEDIA_SIZE;
	else if (!strcmp(mode, "preflight-smmstore"))
		fixture.policy.regions[0].flash_offset = 0x6000;
	else if (!strcmp(mode, "preflight-policy"))
		fixture.policy.regions[0].flags = 0;
	else if (!strcmp(mode, "guard"))
		fixture.sha256_context.drop_guard = true;
	else if (!strcmp(mode, "media-erase"))
		fixture.media_context.fail_erase = true;
	else if (!strcmp(mode, "media-write"))
		fixture.media_context.fail_write = true;
	else if (!strcmp(mode, "media-read"))
		fixture.media_context.fail_read = true;
	else if (!strcmp(mode, "media-verify"))
		fixture.media_context.corrupt_readback = true;
	install(&fixture);
	if (!strcmp(mode, "guard")) {
		fixture.capsule = intent(&fixture, PAYLOAD_MM_FMP_CAPSULE_SET, 1, 11);
		assert(authenticate_intent(&fixture.capsule) == CB_ERR);
	} else {
		authenticate_set(&fixture, 1, 11);
	}
	if (strcmp(mode, "no-grant") && strcmp(mode, "guard")) {
		uint64_t transaction = !strcmp(mode, "grant-transaction") ? 2 : 1;
		uint32_t version = !strcmp(mode, "grant-version") ? 12 : 11;
		enum cb_err expected = (!strcmp(mode, "grant-transaction") ||
			!strcmp(mode, "grant-version")) ? CB_ERR : CB_SUCCESS;

		assert(checkpoint_grant(GENERATION, transaction,
			version) == expected);
	}
	if (!strcmp(mode, "digest"))
		((struct payload_mm_fmp_capsule_intent *)staged_intent)->digest[0] ^= 1;
	assert(apply_staged() == CB_ERR);
	if (strncmp(mode, "media-", 6)) {
		assert(!fixture.reads && !fixture.erases && !fixture.writes);
	} else if (!strcmp(mode, "media-erase")) {
		assert(!fixture.reads && fixture.erases == 1 && !fixture.writes);
	} else if (!strcmp(mode, "media-write")) {
		assert(!fixture.reads && fixture.erases == 1 && fixture.writes == 1);
	} else {
		assert(fixture.reads == 1 && fixture.erases == 1 &&
			fixture.writes == 1);
	}
	assert(apply_staged() == CB_ERR);
}

static void stale_then_happy(void)
{
	struct fixture fixture;

	initialize(&fixture);
	install(&fixture);
	fixture.capsule = intent(&fixture, PAYLOAD_MM_FMP_CAPSULE_SET, 1, 11);
	fixture.capsule.broker_generation++;
	assert(authenticate_intent(&fixture.capsule) == CB_ERR);
	authenticate_set(&fixture, 1, 11);
	assert(checkpoint_grant(GENERATION, 1, 11) == CB_SUCCESS);
	assert(apply_staged() == CB_SUCCESS);
}

static void close_case(bool s3)
{
	struct fixture fixture;

	initialize(&fixture);
	install(&fixture);
	if (s3) {
		capsule_broker_close_for_s3();
		assert(apply_staged() == CB_ERR);
		return;
	}
	capsule_broker_close_for_s3();
	assert(apply_staged() == CB_ERR);
}

static void policy_snapshot_case(void)
{
	struct fixture fixture;
	struct fixture decoy;

	initialize(&fixture);
	initialize(&decoy);
	install(&fixture);
	fixture.policy.regions[0].flash_offset = 0x4000;
	fixture.policy.media.write = NULL;
	fixture.media_context.bytes = decoy.media;
	fixture.media_context.reads = &decoy.reads;
	fixture.media_context.erases = &decoy.erases;
	fixture.media_context.writes = &decoy.writes;
	fixture.authenticate_context.allow = false;
	fixture.policy.authenticate = NULL;
	fixture.proof_context.communication = (uintptr_t)decoy.communication;
	fixture.proof_context.staging = (uintptr_t)decoy.staging;
	fixture.proof_context.dma_protected = false;
	fixture.proof_context.smm_spi_owned = false;
	fixture.proof_context.no_raw_flash = false;
	fixture.proof_context.rendezvous = false;
	authenticate_set(&fixture, 1, 11);
	assert(checkpoint_grant(GENERATION, 1, 11) == CB_SUCCESS);
	assert(apply_staged() == CB_SUCCESS);
	assert(!memcmp(&fixture.media[0x1000], fixture.staging, IMAGE_SIZE));
	assert(!memcmp(&fixture.media[0x4000], &fixture.original[0x4000],
		IMAGE_SIZE));
}

static void install_source_mutation_case(void)
{
	struct fixture fixture;
	uint64_t original_communication;

	initialize(&fixture);
	original_communication = fixture.proof_context.communication;
	install_mutation_target = &fixture.proof_context;
	install_auth_mutation_target = &fixture.authenticate_context;
	install(&fixture);
	assert(install_mutation_target == NULL);
	assert(install_auth_mutation_target == NULL);
	assert(fixture.proof_context.communication == original_communication + 8);
	assert(!fixture.authenticate_context.allow);
	authenticate_set(&fixture, 1, 11);
	assert(checkpoint_grant(GENERATION, 1, 11) == CB_SUCCESS);
	assert(apply_staged() == CB_SUCCESS);
}

static void grant_edges(void)
{
	struct fixture fixture;

	initialize(&fixture);
	install(&fixture);
	authenticate_set(&fixture, UINT64_MAX, 11);
	assert(checkpoint_grant(GENERATION + 1, 1, 11) == CB_ERR);
	assert(checkpoint_grant(GENERATION, 0, 11) == CB_ERR);
	assert(checkpoint_grant(GENERATION, UINT64_MAX, 11) ==
		CB_SUCCESS);
	assert(checkpoint_grant(GENERATION, UINT64_MAX, 11) ==
		CB_ERR);
	assert(apply_staged() == CB_SUCCESS);
}

static void authentication_case(const char *mode)
{
	struct fixture fixture;
	struct payload_mm_fmp_capsule_intent capsule;

	initialize(&fixture);
	if (!strcmp(mode, "reject"))
		fixture.authenticate_context.allow = false;
	install(&fixture);
	capsule = intent(&fixture, PAYLOAD_MM_FMP_CAPSULE_CHECK, 1, 11);
	if (!strcmp(mode, "source-mutation")) {
		fixture.authenticate_context.allow = false;
		fixture.policy.authenticate = NULL;
	} else if (!strcmp(mode, "context-mutation")) {
		authenticate_mutates_context = true;
	} else if (!strcmp(mode, "image-mutation")) {
		authenticate_mutates_image = true;
	} else if (!strcmp(mode, "owner-mutation")) {
		authenticate_mutates_owner = true;
	} else if (!strcmp(mode, "guard")) {
		authenticate_drops_guard = true;
	} else if (!strcmp(mode, "digest")) {
		capsule.digest[0] ^= 1;
	} else if (!strcmp(mode, "generation")) {
		capsule.broker_generation++;
	} else if (!strcmp(mode, "image-size")) {
		capsule.image_size--;
	} else if (!strcmp(mode, "operation")) {
		capsule.operation = 3;
	} else if (!strcmp(mode, "flags")) {
		capsule.flags = 1;
	} else if (!strcmp(mode, "revision")) {
		capsule.revision++;
	} else if (!strcmp(mode, "size")) {
		capsule.size--;
	} else if (!strcmp(mode, "transaction")) {
		capsule.transaction = 0;
	} else if (!strcmp(mode, "algorithm")) {
		capsule.digest_algorithm++;
	} else if (!strcmp(mode, "digest-size")) {
		capsule.digest_size--;
	} else if (!strcmp(mode, "reserved")) {
		capsule.reserved = 1;
	} else if (!strcmp(mode, "version")) {
		capsule.attempted_version++;
	} else if (!strcmp(mode, "closed")) {
		capsule_broker_close_for_s3();
	} else if (!strcmp(mode, "unstaged")) {
		assert(capsule_broker_authenticate_intent_bound(&capsule,
			&owner_record) == CB_ERR);
		assert(authenticate_calls == 0);
		return;
	}
	if (!strcmp(mode, "reenter"))
		authenticate_reenters = true;
	else if (!strcmp(mode, "callback-grant")) {
		authenticate_reenters = true;
		authenticate_attempts_grant = true;
		capsule.operation = PAYLOAD_MM_FMP_CAPSULE_SET;
	} else if (!strcmp(mode, "callback-apply")) {
		authenticate_attempts_apply = true;
		capsule.operation = PAYLOAD_MM_FMP_CAPSULE_SET;
	} else if (!strcmp(mode, "callback-close")) {
		authenticate_attempts_close = true;
	} else if (!strcmp(mode, "callback-mutation")) {
		authenticate_mutates_intent = true;
		capsule.operation = PAYLOAD_MM_FMP_CAPSULE_SET;
	}
	if (!strcmp(mode, "reenter")) {
		assert(authenticate_intent(&capsule) == CB_SUCCESS);
		assert(nested_authenticate_status == CB_ERR);
		assert(authenticate_calls == 1);
		assert(authenticate_intent(&capsule) == CB_SUCCESS);
		assert(nested_authenticate_status == CB_ERR);
		assert(authenticate_calls == 2);
		return;
	}
	if (!strcmp(mode, "callback-grant") ||
	    !strcmp(mode, "callback-apply")) {
		assert(authenticate_intent(&capsule) == CB_SUCCESS);
		if (!strcmp(mode, "callback-grant"))
			assert(nested_authenticate_status == CB_ERR);
		assert((!strcmp(mode, "callback-grant") ? nested_grant_status :
			nested_apply_status) == CB_ERR);
		assert(checkpoint_grant(GENERATION, 1, 11) ==
			CB_SUCCESS);
		return;
	}
	if (!strcmp(mode, "callback-close")) {
		assert(authenticate_intent(&capsule) == CB_ERR);
		assert(!capsule_broker_generation_matches(GENERATION));
		assert(checkpoint_grant(GENERATION, 1, 11) == CB_ERR);
		return;
	}
	if (!strcmp(mode, "callback-mutation")) {
		uint8_t expected_digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];

		calculate_digest(fixture.staging, sizeof(fixture.staging),
			expected_digest);
		assert(authenticate_intent(&capsule) == CB_SUCCESS);
		assert(capsule.transaction == 2 && capsule.digest[0] !=
			expected_digest[0]);
		assert(checkpoint_grant(GENERATION, 2, 11) == CB_ERR);
		assert(checkpoint_grant(GENERATION, 1, 11) == CB_ERR);
		return;
	}
	if (!strcmp(mode, "happy") || !strcmp(mode, "source-mutation") ||
	    !strcmp(mode, "context-mutation") || !strcmp(mode, "check-only")) {
		assert(authenticate_intent(&capsule) == CB_SUCCESS);
		assert(authenticate_calls == 1);
		assert(checkpoint_grant(GENERATION, 1, 11) == CB_ERR);
		if (!strcmp(mode, "context-mutation")) {
			capsule.transaction++;
			assert(authenticate_intent(&capsule) == CB_SUCCESS);
			assert(authenticate_calls == 2);
		}
		return;
	}
	if (!strcmp(mode, "set-one-shot")) {
		struct payload_mm_fmp_capsule_intent second;

		capsule.operation = PAYLOAD_MM_FMP_CAPSULE_SET;
		assert(authenticate_intent(&capsule) == CB_SUCCESS);
		second = capsule;
		second.transaction++;
		assert(authenticate_intent(&second) == CB_ERR);
		assert(checkpoint_grant(GENERATION, 2, 11) == CB_ERR);
		assert(checkpoint_grant(GENERATION, 1, 11) ==
			CB_SUCCESS);
		assert(authenticate_intent(&second) == CB_ERR);
		assert(authenticate_calls == 1);
		return;
	}
	assert(authenticate_intent(&capsule) == CB_ERR);
	if (!strcmp(mode, "digest") || !strcmp(mode, "generation") ||
	    !strcmp(mode, "image-size") || !strcmp(mode, "operation") ||
	    !strcmp(mode, "flags") || !strcmp(mode, "revision") ||
	    !strcmp(mode, "size") || !strcmp(mode, "transaction") ||
	    !strcmp(mode, "algorithm") || !strcmp(mode, "digest-size") ||
	    !strcmp(mode, "reserved") || !strcmp(mode, "closed") ||
	    !strcmp(mode, "unstaged")) {
		assert(authenticate_calls == 0);
	} else {
		assert(authenticate_calls == 1);
	}
	assert(checkpoint_grant(GENERATION, 1, 11) == CB_ERR);
	assert(!fixture.reads && !fixture.erases && !fixture.writes);
	if (!strcmp(mode, "guard")) {
		authenticate_drops_guard = false;
		runtime_dma_guard = true;
		assert(authenticate_intent(&capsule) == CB_SUCCESS);
		assert(authenticate_calls == 2);
	}
}

static void callback_boundary_case(const char *mode)
{
	struct fixture fixture;
	struct payload_mm_fmp_capsule_intent capsule;
	const bool proof = !strncmp(mode, "proof-", 6);
	const bool close = strstr(mode, "-close-") != NULL;
	const size_t target = (size_t)(mode[strlen(mode) - 1] - '0');
	size_t expected_proofs;

	initialize(&fixture);
	install(&fixture);
	memset(proof_calls, 0, sizeof(proof_calls));
	sha256_calls = 0;
	capsule = intent(&fixture, PAYLOAD_MM_FMP_CAPSULE_SET, 1, 11);
	callback_attack = close ? CALLBACK_ATTACK_CLOSE :
		CALLBACK_ATTACK_REENTER_GRANT;
	if (proof)
		proof_attack_target = (enum proof_callback)target;
	else
		sha256_attack_call = target;
	if (close) {
		assert(authenticate_intent(&capsule) == CB_ERR);
		assert(!capsule_broker_generation_matches(GENERATION));
		assert(checkpoint_grant(GENERATION, 1, 11) == CB_ERR);
		if (proof) {
			for (size_t i = PROOF_DMA_COMMUNICATION;
			     i < PROOF_COUNT; i++)
				assert(proof_calls[i] == (i <= target));
			assert(sha256_calls == 0);
			assert(authenticate_calls == 0);
		} else {
			expected_proofs = target == 1 ? 1 : 3;
			for (size_t i = PROOF_DMA_COMMUNICATION;
			     i < PROOF_COUNT; i++)
				assert(proof_calls[i] == expected_proofs);
			assert(sha256_calls == target);
			assert(authenticate_calls == (target == 2));
		}
		return;
	}
	assert(authenticate_intent(&capsule) == CB_SUCCESS);
	assert(nested_authenticate_status == CB_ERR);
	assert(nested_grant_status == CB_ERR);
	assert(authenticate_calls == 1);
	for (size_t i = PROOF_DMA_COMMUNICATION; i < PROOF_COUNT; i++)
		assert(proof_calls[i] == 4);
	assert(sha256_calls == 2);
	assert(checkpoint_grant(GENERATION, 1, 11) == CB_SUCCESS);
}

int main(int argc, char **argv)
{
	if (argc == 1)
		happy();
	else if (!strcmp(argv[1], "endpoint"))
		endpoint_mutations();
	else if (!strcmp(argv[1], "stale"))
		stale_then_happy();
	else if (!strcmp(argv[1], "close"))
		close_case(false);
	else if (!strcmp(argv[1], "s3"))
		close_case(true);
	else if (!strcmp(argv[1], "policy-snapshot"))
		policy_snapshot_case();
	else if (!strcmp(argv[1], "source-mutation"))
		install_source_mutation_case();
	else if (!strcmp(argv[1], "grant-edges"))
		grant_edges();
	else if (!strcmp(argv[1], "generation-match"))
		generation_match_case();
	else if (!strncmp(argv[1], "bound-", 6))
		bound_transaction_case(argv[1] + 6);
	else if (!strncmp(argv[1], "authenticate-", 13))
		authentication_case(argv[1] + 13);
	else if (!strncmp(argv[1], "proof-close-", 12) ||
		 !strncmp(argv[1], "proof-reenter-", 14) ||
		 !strncmp(argv[1], "hash-close-", 11) ||
		 !strncmp(argv[1], "hash-reenter-", 13))
		callback_boundary_case(argv[1]);
	else if (!strncmp(argv[1], "install-", 8))
		invalid_install(argv[1] + 8);
	else
		rejected_apply(argv[1]);
	return 0;
}
