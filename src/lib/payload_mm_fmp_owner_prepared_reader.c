/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <security/tpm/tss/tcg-2.0/tss_structures.h>
#include <string.h>

#include "payload_mm_fmp_owner_prepared_reader_internal.h"

#if ENV_SMM && !ENV_TEST
#error "The owner PREPARED reader must not be built in SMM"
#endif

#define READER_RUNNING 0x50525231U
#define READER_FINISHED 0x50524631U

_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
	"owner PREPARED reader control must be lock-free");

struct slot_image {
	struct payload_mm_fmp_owner_journal_manifest manifest;
	struct payload_mm_fmp_owner_prepared prepared;
};

struct proof_scan {
	struct slot_image current;
	struct slot_image candidate;
	size_t current_domain;
	u32 current_slot;
	size_t candidate_domain;
	u32 candidate_slot;
	u32 current_count;
	u32 candidate_count;
	u32 prepared_count;
};

static bool bytes_equal_value(const u8 *data, size_t size, u8 value)
{
	u8 difference = 0;

	for (size_t i = 0; i < size; i++)
		difference |= data[i] ^ value;
	return difference == 0;
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_start = (uintptr_t)left;
	uintptr_t right_start = (uintptr_t)right;

	if (!left || !right || !left_size || !right_size ||
	    left_start > UINTPTR_MAX - left_size ||
	    right_start > UINTPTR_MAX - right_size)
		return true;
	return left_start < right_start + right_size &&
		right_start < left_start + left_size;
}

static const struct region_device *root_device(
	const struct region_device *device)
{
	return device->root ? device->root : device;
}

static bool region_contains(const struct region *outer,
	const struct region *inner)
{
	return outer->size && inner->size &&
		outer->offset <= SIZE_MAX - outer->size &&
		inner->offset <= SIZE_MAX - inner->size &&
		inner->offset >= outer->offset &&
		inner->offset + inner->size <= outer->offset + outer->size;
}

static bool policy_unchanged(
	struct payload_mm_fmp_owner_prepared_reader *reader)
{
	bool unchanged = !memcmp(&reader->policy, &reader->sealed_policy,
		sizeof(reader->policy));

	for (size_t domain = 0;
	     domain < PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS; domain++) {
		const struct payload_mm_fmp_owner_prepared_media *media =
			&reader->sealed_policy.media[domain];

		unchanged = unchanged && media->root_device &&
			!memcmp(media->root_device, &media->root,
				sizeof(media->root)) && media->root_device->ops &&
			!memcmp(media->root_device->ops, &media->ops,
				sizeof(media->ops));
	}
	if (!unchanged)
		reader->poisoned = true;
	return unchanged;
}

static bool proof_active(
	struct payload_mm_fmp_owner_prepared_reader *reader)
{
	return reader->initialized && !reader->poisoned &&
		__atomic_load_n(&reader->control, __ATOMIC_ACQUIRE) ==
			READER_RUNNING && policy_unchanged(reader);
}

static enum cb_err hash(struct payload_mm_fmp_owner_prepared_reader *reader,
	const void *buffer, size_t size,
	u8 digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE])
{
	union {
		struct fmp_owner_layout layout;
		struct payload_mm_fmp_owner_journal_manifest manifest;
	} snapshot_storage;
	u8 *snapshot = (void *)&snapshot_storage;
	enum cb_err result;

	if (!buffer || !size || size > sizeof(snapshot_storage) || reader->poisoned ||
	    !policy_unchanged(reader))
		return CB_ERR;
	memcpy(snapshot, buffer, size);
	result = reader->policy.sha256(reader->policy.context, buffer, size,
		digest);
	if (!policy_unchanged(reader) || memcmp(snapshot, buffer, size))
		return CB_ERR;
	return result;
}

static enum cb_err read_slot(
	struct payload_mm_fmp_owner_prepared_reader *reader, size_t domain,
	u32 slot, struct slot_image *image)
{
	const struct payload_mm_fmp_owner_prepared_media *media;
	size_t offset;
	ssize_t result;

	if (domain >= PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS || !image ||
	    slot >= reader->policy.layout.state[domain].size /
		reader->policy.layout.slot_size || !proof_active(reader))
		return CB_ERR;
	media = &reader->policy.media[domain];
	if ((size_t)slot > SIZE_MAX / reader->policy.layout.slot_size)
		return CB_ERR;
	offset = (size_t)slot * reader->policy.layout.slot_size;
	memset(image, 0, sizeof(*image));
	result = media->ops.readat(media->root_device, image,
		media->state.region.offset + offset, sizeof(*image));
	if (!proof_active(reader) || result != sizeof(*image)) {
		memset(image, 0, sizeof(*image));
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static enum cb_err manifest_anchor(
	struct payload_mm_fmp_owner_prepared_reader *reader,
	const struct payload_mm_fmp_owner_journal_manifest *manifest,
	struct payload_mm_fmp_owner_journal_anchor *anchor)
{
	if (!payload_mm_fmp_owner_journal_manifest_shape_valid(manifest,
		reader->policy.layout.slot_size) ||
	    !payload_mm_fmp_owner_journal_manifest_records_valid(manifest) ||
	    memcmp(manifest->storage_domain, reader->policy.storage_domain,
		sizeof(manifest->storage_domain)) ||
	    bytes_equal_value(manifest->identity_binding,
		sizeof(manifest->identity_binding), 0))
		return CB_ERR;
	anchor->epoch = manifest->epoch;
	if (hash(reader, manifest, sizeof(*manifest), anchor->digest) != CB_SUCCESS ||
	    !proof_active(reader))
		return CB_ERR;
	return CB_SUCCESS;
}

static bool grant_tuple_equal(
	const struct payload_mm_fmp_owner_prepared *prepared,
	const struct capsule_tpm_anchor_grant *grant)
{
	return prepared->generation == grant->generation &&
		prepared->transaction == grant->transaction &&
		prepared->current.epoch == grant->current.epoch &&
		!memcmp(prepared->current.digest, grant->current.digest,
			sizeof(prepared->current.digest)) &&
		prepared->candidate.epoch == grant->candidate.epoch &&
		!memcmp(prepared->candidate.digest, grant->candidate.digest,
			sizeof(prepared->candidate.digest));
}

static bool anchor_matches_value(
	const struct payload_mm_fmp_owner_journal_anchor *anchor,
	const struct capsule_tpm_anchor_value *value)
{
	return anchor->epoch == value->epoch &&
		!memcmp(anchor->digest, value->digest, sizeof(anchor->digest));
}

static enum cb_err inspect_slot(
	struct payload_mm_fmp_owner_prepared_reader *reader,
	const struct capsule_tpm_anchor_grant *grant, size_t domain, u32 slot,
	struct proof_scan *scan)
{
	struct payload_mm_fmp_owner_journal_anchor anchor;
	struct slot_image image;
	bool prepared_erased;
	bool manifest_valid;

	if (read_slot(reader, domain, slot, &image) != CB_SUCCESS)
		return CB_ERR;
	prepared_erased = bytes_equal_value((const u8 *)&image.prepared,
		sizeof(image.prepared), 0xff);
	manifest_valid = manifest_anchor(reader, &image.manifest, &anchor) ==
		CB_SUCCESS;
	if (!prepared_erased) {
		if (!payload_mm_fmp_owner_journal_prepared_shape_valid(
			&image.prepared) || !manifest_valid ||
		    !payload_mm_fmp_owner_journal_anchor_equal(&anchor,
			&image.prepared.candidate))
			return CB_ERR;
		if (image.prepared.state == PAYLOAD_MM_FMP_OWNER_PREPARED_STATE) {
			if (!grant_tuple_equal(&image.prepared, grant))
				return CB_ERR;
			scan->prepared_count++;
		}
	}
	if (!manifest_valid)
		return CB_SUCCESS;
	if (anchor_matches_value(&anchor, &grant->current)) {
		if (scan->current_count && memcmp(&scan->current.manifest,
			&image.manifest, sizeof(image.manifest)))
			return CB_ERR;
		scan->current = image;
		scan->current_domain = domain;
		scan->current_slot = slot;
		scan->current_count++;
	}
	if (anchor_matches_value(&anchor, &grant->candidate)) {
		scan->candidate = image;
		scan->candidate_domain = domain;
		scan->candidate_slot = slot;
		scan->candidate_count++;
	}
	return CB_SUCCESS;
}

static enum cb_err scan_media(
	struct payload_mm_fmp_owner_prepared_reader *reader,
	const struct capsule_tpm_anchor_grant *grant, struct proof_scan *scan)
{
	memset(scan, 0, sizeof(*scan));
	for (size_t domain = 0;
	     domain < PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS; domain++) {
		u32 slots = (u32)(reader->policy.layout.state[domain].size /
			reader->policy.layout.slot_size);

		for (u32 slot = 0; slot < slots; slot++)
			if (inspect_slot(reader, grant, domain, slot, scan) !=
			    CB_SUCCESS)
				return CB_ERR;
	}
	return scan->current_count && scan->candidate_count == 1 &&
		scan->prepared_count == 1 ? CB_SUCCESS : CB_ERR;
}

static bool grant_valid(
	const struct payload_mm_fmp_owner_prepared_reader *reader,
	const struct capsule_tpm_anchor_grant *grant)
{
	return grant->revision == CAPSULE_TPM_ANCHOR_GRANT_REVISION &&
		grant->size == sizeof(*grant) &&
		grant->policy_revision == CAPSULE_TPM_ANCHOR_POLICY_REVISION &&
		(grant->nv_index & 0xff000000U) == HR_NV_INDEX &&
		grant->generation && grant->transaction && !grant->flags &&
		!grant->reserved &&
		grant->current.epoch && grant->current.epoch != UINT64_MAX &&
		!bytes_equal_value(grant->current.digest,
			sizeof(grant->current.digest), 0) &&
		grant->candidate.epoch &&
		!bytes_equal_value(grant->candidate.digest,
			sizeof(grant->candidate.digest), 0) &&
		grant->candidate.epoch == grant->current.epoch + 1 &&
		anchor_matches_value(&reader->policy.current, &grant->current);
}

enum cb_err payload_mm_fmp_owner_prepared_reader_init(
	struct payload_mm_fmp_owner_prepared_reader *reader,
	const struct fmp_owner_layout *layout,
	const struct region_device state[PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS],
	const struct payload_mm_fmp_owner_journal_anchor *current,
	payload_mm_fmp_owner_prepared_sha256_fn *sha256,
	const void *context, size_t context_size)
{
	struct payload_mm_fmp_owner_prepared_policy policy = { 0 };
	u8 storage_domain[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];

	if (!reader || !layout || !state || !current || !sha256 ||
	    reader->initialized || reader->control || reader->poisoned ||
	    !payload_mm_fmp_layout_valid(layout) ||
	    layout->slot_size < sizeof(struct slot_image) ||
	    !payload_mm_fmp_owner_journal_anchor_valid(current) ||
	    ((context == NULL) != (context_size == 0)) ||
	    context_size > sizeof(policy.context) ||
	    ranges_overlap(reader, sizeof(*reader), layout, sizeof(*layout)) ||
	    ranges_overlap(reader, sizeof(*reader), state,
		PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS * sizeof(*state)) ||
	    ranges_overlap(reader, sizeof(*reader), current, sizeof(*current)) ||
	    (context && ranges_overlap(reader, sizeof(*reader), context,
		context_size)))
		return CB_ERR;
	policy.revision = PAYLOAD_MM_FMP_OWNER_PREPARED_READER_REVISION;
	policy.size = sizeof(policy);
	policy.layout = *layout;
	policy.current = *current;
	policy.sha256 = sha256;
	policy.context_size = context_size;
	if (context_size)
		memcpy(policy.context, context, context_size);
	for (size_t domain = 0;
	     domain < PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS; domain++) {
		const struct region_device *root = root_device(&state[domain]);
		struct payload_mm_fmp_owner_prepared_media *media =
			&policy.media[domain];

		if (!root || !root->ops || !root->ops->readat ||
		    region_device_offset(&state[domain]) !=
			layout->state[domain].offset ||
		    region_device_sz(&state[domain]) != layout->state[domain].size ||
		    !region_contains(&root->region, &state[domain].region))
			return CB_ERR;
		media->state = state[domain];
		media->root_device = root;
		media->root = *root;
		media->ops = *root->ops;
	}
	if (policy.media[0].root_device != policy.media[1].root_device ||
	    policy.media[0].root_device->root ||
	    region_device_offset(policy.media[0].root_device) != 0 ||
	    region_device_sz(policy.media[0].root_device) != layout->media_size)
		return CB_ERR;
	reader->policy = policy;
	reader->sealed_policy = policy;
	if (hash(reader, &reader->policy.layout, sizeof(reader->policy.layout),
		storage_domain) != CB_SUCCESS ||
	    bytes_equal_value(storage_domain, sizeof(storage_domain), 0)) {
		memset(reader, 0, sizeof(*reader));
		reader->poisoned = true;
		return CB_ERR;
	}
	memcpy(reader->policy.storage_domain, storage_domain,
		sizeof(storage_domain));
	memcpy(reader->sealed_policy.storage_domain, storage_domain,
		sizeof(storage_domain));
	reader->initialized = true;
	return CB_SUCCESS;
}

enum cb_err payload_mm_fmp_owner_prepared_reader_prove(const void *context,
	const struct capsule_tpm_anchor_grant *grant)
{
	struct payload_mm_fmp_owner_prepared_reader *reader = (void *)context;
	struct capsule_tpm_anchor_grant snapshot;
	struct slot_image current;
	struct slot_image candidate;
	struct proof_scan scan;
	u32 expected = 0;
	enum cb_err result = CB_ERR;

	if (!reader || !grant || !reader->initialized || reader->poisoned ||
	    !__atomic_compare_exchange_n(&reader->control, &expected,
		READER_RUNNING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	snapshot = *grant;
	if (!proof_active(reader) || !grant_valid(reader, &snapshot) ||
	    memcmp(grant, &snapshot, sizeof(snapshot)) ||
	    scan_media(reader, &snapshot, &scan) != CB_SUCCESS ||
	    memcmp(scan.current.manifest.identity_binding,
		scan.candidate.manifest.identity_binding,
		sizeof(scan.current.manifest.identity_binding)) ||
	    read_slot(reader, scan.current_domain, scan.current_slot, &current) !=
		CB_SUCCESS ||
	    read_slot(reader, scan.candidate_domain, scan.candidate_slot,
		&candidate) != CB_SUCCESS ||
	    memcmp(&current, &scan.current, sizeof(current)) ||
	    memcmp(&candidate, &scan.candidate, sizeof(candidate)) ||
	    memcmp(grant, &snapshot, sizeof(snapshot)) ||
	    !proof_active(reader))
		goto out;
	result = CB_SUCCESS;
out:
	__atomic_store_n(&reader->control, READER_FINISHED, __ATOMIC_RELEASE);
	memset(&snapshot, 0, sizeof(snapshot));
	memset(&scan, 0, sizeof(scan));
	memset(&current, 0, sizeof(current));
	memset(&candidate, 0, sizeof(candidate));
	return result;
}
