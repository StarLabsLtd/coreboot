/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <security/tpm/capsule_anchor_grant.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_owner_journal_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM FMP owner journal must only be built in SMM"
#endif

struct journal_policy {
	struct payload_mm_fmp_owner_journal_port port;
	uint8_t context[PAYLOAD_MM_FMP_OWNER_JOURNAL_CONTEXT_SIZE] __aligned(8);
};

static struct {
	struct journal_policy policy;
	struct journal_policy sealed_policy;
	struct payload_mm_fmp_state_identity
		identity[PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS];
	uint8_t identity_binding[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];
	u8 storage_domain[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];
	bool installed;
	bool install_attempted;
	bool busy;
	bool poisoned;
} journal;

#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
enum slot_anchor_result {
	SLOT_ANCHOR_ERROR,
	SLOT_ANCHOR_INVALID,
	SLOT_ANCHOR_VALID,
};
#endif

struct journal_scan {
	struct payload_mm_fmp_owner_journal_anchor anchor;
	struct payload_mm_fmp_owner_journal_manifest manifest;
#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
	struct payload_mm_fmp_owner_prepared prepared;
	struct payload_mm_fmp_owner_transition_material material;
	bool authorized;
#endif
	uint32_t last_occupied[PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS];
	bool occupied[PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS];
	bool authoritative[PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS];
};

static bool anchor_equal(
	const struct payload_mm_fmp_owner_journal_anchor *left,
	const struct payload_mm_fmp_owner_journal_anchor *right);
static enum cb_err manifest_digest(
	const struct payload_mm_fmp_owner_journal_manifest *manifest,
	uint8_t digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE]);
#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
static enum cb_err write_prepared(uint64_t slot_offset,
	const struct payload_mm_fmp_owner_prepared *prepared);
static enum cb_err write_transition_material(uint64_t slot_offset,
	const struct payload_mm_fmp_owner_transition_material *material);
#endif

static bool bytes_equal_value(const uint8_t *data, size_t size, uint8_t value)
{
	uint8_t difference = 0;

	for (size_t i = 0; i < size; i++)
		difference |= data[i] ^ value;
	return difference == 0;
}

static bool policy_unchanged(void)
{
	struct payload_mm_fmp_owner_journal_port active = journal.policy.port;
	struct payload_mm_fmp_owner_journal_port sealed =
		journal.sealed_policy.port;

	active.context = NULL;
	sealed.context = NULL;
	bool unchanged = !memcmp(&active, &sealed, sizeof(active)) &&
		!memcmp(journal.policy.context, journal.sealed_policy.context,
			journal.policy.port.context_size);

	if (!unchanged)
		journal.poisoned = true;
	return unchanged;
}

static bool journal_storage_overlaps(const void *buffer, size_t size)
{
	return payload_mm_authvar_buffers_overlap(buffer, size, &journal,
		sizeof(journal));
}

static enum cb_err hash(const void *data, size_t size,
	uint8_t digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE])
{
	union {
		struct fmp_owner_layout layout;
		struct payload_mm_fmp_state_identity
			identity[PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS];
		struct capsule_tpm_anchor_authorization authorization;
	} snapshot_storage;
	u8 *snapshot = (void *)&snapshot_storage;
	enum cb_err result;

	if (journal.poisoned || size > sizeof(snapshot_storage))
		return CB_ERR;
	memcpy(snapshot, data, size);
	result = journal.policy.port.sha256(journal.policy.port.context, data, size,
		digest);
	if (!policy_unchanged() || memcmp(snapshot, data, size) != 0)
		return CB_ERR;
	return result;
}

static enum cb_err bind_storage_domain(void)
{
	const void *layout = &journal.policy.port.layout;
	size_t layout_size = sizeof(journal.policy.port.layout);
	enum cb_err status = hash(layout, layout_size, journal.storage_domain);
	bool cleared = bytes_equal_value(journal.storage_domain,
		sizeof(journal.storage_domain), 0);

	if (status || cleared)
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err media_read(uint64_t offset, void *buffer, size_t size)
{
	enum cb_err result;

	if (journal.poisoned)
		return CB_ERR;
	result = journal.policy.port.read(journal.policy.port.context, offset,
		buffer, size);

	return policy_unchanged() ? result : CB_ERR;
}

static enum cb_err media_program(uint64_t offset, const void *buffer,
	size_t size)
{
	union {
		struct payload_mm_fmp_owner_journal_manifest manifest;
		struct payload_mm_fmp_owner_transition_material material;
	} snapshot_storage;
	uint8_t *snapshot = (void *)&snapshot_storage;
	enum cb_err result;

	if (journal.poisoned ||
	    (size != sizeof(struct payload_mm_fmp_owner_journal_manifest) &&
	     size != sizeof(struct payload_mm_fmp_owner_prepared) &&
	     size != sizeof(struct payload_mm_fmp_owner_transition_material) &&
	     size != sizeof(uint32_t)))
		return CB_ERR;
	memcpy(snapshot, buffer, size);
	result = journal.policy.port.program(journal.policy.port.context, offset,
		buffer, size);
	if (!policy_unchanged() || memcmp(snapshot, buffer, size) != 0)
		return CB_ERR;
	return result;
}

#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
static bool prepared_shape_valid(
	const struct payload_mm_fmp_owner_prepared *prepared)
{
	return payload_mm_fmp_owner_journal_prepared_shape_valid(prepared);
}

static enum cb_err prepared_read(uint64_t slot_offset,
	struct payload_mm_fmp_owner_prepared *prepared, bool *erased)
{
	uint64_t offset = slot_offset +
		sizeof(struct payload_mm_fmp_owner_journal_manifest);

	if (journal.policy.port.layout.slot_size <
	    sizeof(struct payload_mm_fmp_owner_journal_manifest) +
	    sizeof(*prepared)) {
		memset(prepared, 0xff, sizeof(*prepared));
		*erased = true;
		return CB_SUCCESS;
	}
	memset(prepared, 0, sizeof(*prepared));
	if (media_read(offset, prepared, sizeof(*prepared)) != CB_SUCCESS)
		return CB_ERR;
	*erased = bytes_equal_value((const uint8_t *)prepared,
		sizeof(*prepared), 0xff);
	return CB_SUCCESS;
}
#endif

static enum cb_err media_sync(void)
{
	enum cb_err result;

	if (journal.poisoned)
		return CB_ERR;
	result = journal.policy.port.sync(journal.policy.port.context);

	return policy_unchanged() ? result : CB_ERR;
}

static bool anchor_equal(
	const struct payload_mm_fmp_owner_journal_anchor *left,
	const struct payload_mm_fmp_owner_journal_anchor *right)
{
	return payload_mm_fmp_owner_journal_anchor_equal(left, right);
}

static bool anchor_cleared(
	const struct payload_mm_fmp_owner_journal_anchor *anchor)
{
	return !anchor->epoch &&
		bytes_equal_value(anchor->digest, sizeof(anchor->digest), 0);
}

static enum cb_err anchor_read(
	struct payload_mm_fmp_owner_journal_anchor *anchor)
{
	if (journal.poisoned)
		return CB_ERR;
	memset(anchor, 0, sizeof(*anchor));
	if (journal.policy.port.anchor_read(journal.policy.port.context, anchor) !=
		CB_SUCCESS || !policy_unchanged())
		return CB_ERR;
	return CB_SUCCESS;
}

static bool manifest_shape_valid(
	const struct payload_mm_fmp_owner_journal_manifest *manifest)
{
	if (!payload_mm_fmp_owner_journal_manifest_shape_valid(manifest,
		journal.policy.port.layout.slot_size) ||
	    memcmp(manifest->storage_domain,
		journal.storage_domain,
		sizeof(manifest->storage_domain)) != 0 ||
	    memcmp(manifest->identity_binding, journal.identity_binding,
		sizeof(manifest->identity_binding)) != 0)
		return false;
	return payload_mm_fmp_owner_journal_manifest_records_valid(manifest);
}

static enum cb_err slot_erased(uint64_t offset, bool *erased)
{
	uint8_t chunk[64];
	u32 remaining = journal.policy.port.layout.slot_size;

	*erased = true;
	while (remaining) {
		u32 size = remaining < sizeof(chunk) ? remaining : sizeof(chunk);

		if (media_read(offset, chunk, size) != CB_SUCCESS)
			return CB_ERR;
		if (!bytes_equal_value(chunk, size, 0xff))
			*erased = false;
		offset += size;
		remaining -= size;
	}
	return CB_SUCCESS;
}

static enum cb_err manifest_digest(
	const struct payload_mm_fmp_owner_journal_manifest *manifest,
	uint8_t digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE])
{
	if (!manifest_shape_valid(manifest))
		return CB_ERR;
	return hash(manifest, sizeof(*manifest), digest);
}

#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
static bool authorization_tuple_equal(
	const struct payload_mm_fmp_owner_prepared *prepared,
	const struct capsule_tpm_anchor_authorization *authorization)
{
	return prepared->generation == authorization->generation &&
		prepared->transaction == authorization->transaction &&
		!memcmp(&prepared->current, &authorization->current,
			sizeof(prepared->current)) &&
		!memcmp(&prepared->candidate, &authorization->candidate,
			sizeof(prepared->candidate));
}

static enum cb_err transition_material_read(uint64_t slot_offset,
	struct payload_mm_fmp_owner_transition_material *material, bool *erased)
{
	uint64_t offset = slot_offset +
		sizeof(struct payload_mm_fmp_owner_journal_manifest) +
		sizeof(struct payload_mm_fmp_owner_prepared);

	if (journal.policy.port.layout.slot_size <
	    sizeof(struct payload_mm_fmp_owner_journal_manifest) +
	    sizeof(struct payload_mm_fmp_owner_prepared) + sizeof(*material)) {
		memset(material, 0xff, sizeof(*material));
		*erased = true;
		return CB_SUCCESS;
	}
	memset(material, 0, sizeof(*material));
	if (media_read(offset, material, sizeof(*material)) != CB_SUCCESS)
		return CB_ERR;
	*erased = bytes_equal_value((const uint8_t *)material,
		sizeof(*material), 0xff);
	return CB_SUCCESS;
}

static enum slot_anchor_result authorized_anchor_digest(
	const struct payload_mm_fmp_owner_journal_manifest *manifest,
	const struct payload_mm_fmp_owner_journal_anchor *current,
	uint64_t generation, uint64_t transaction,
	const struct payload_mm_fmp_owner_transition_material *material,
	uint8_t digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE])
{
	struct payload_mm_fmp_owner_authorized_anchor_input input;
	uint8_t authorization_digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];

	if (hash(&material->authorization, sizeof(material->authorization),
		authorization_digest) != CB_SUCCESS)
		return SLOT_ANCHOR_ERROR;
	if (!payload_mm_fmp_owner_transition_material_valid(material,
		authorization_digest) ||
	    !payload_mm_fmp_owner_authorized_anchor_input(manifest,
		current, generation, transaction, &material->receipt, &input))
		return SLOT_ANCHOR_INVALID;
	return hash(&input, sizeof(input), digest) == CB_SUCCESS ?
		SLOT_ANCHOR_VALID : SLOT_ANCHOR_ERROR;
}

static enum slot_anchor_result slot_anchor(uint64_t slot_offset,
	const struct payload_mm_fmp_owner_journal_manifest *manifest,
	struct payload_mm_fmp_owner_journal_anchor *anchor,
	struct payload_mm_fmp_owner_prepared *prepared,
	struct payload_mm_fmp_owner_transition_material *material,
	bool *companion_erased, bool *authorized)
{
	struct payload_mm_fmp_owner_journal_anchor legacy;
	enum slot_anchor_result result;
	bool material_erased;

	*authorized = false;
	memset(material, 0, sizeof(*material));
	legacy.epoch = manifest->epoch;
	if (manifest_digest(manifest, legacy.digest) != CB_SUCCESS ||
	    prepared_read(slot_offset, prepared, companion_erased) != CB_SUCCESS)
		return SLOT_ANCHOR_ERROR;
	if (*companion_erased) {
		*anchor = legacy;
		return SLOT_ANCHOR_VALID;
	}
	if (!prepared_shape_valid(prepared))
		return SLOT_ANCHOR_INVALID;
	if (transition_material_read(slot_offset, material, &material_erased) !=
		CB_SUCCESS)
		return SLOT_ANCHOR_ERROR;
	if (material_erased) {
		if (!anchor_equal(&legacy, &prepared->candidate))
			return SLOT_ANCHOR_INVALID;
		*anchor = legacy;
		return SLOT_ANCHOR_VALID;
	}
	anchor->epoch = manifest->epoch;
	result = authorized_anchor_digest(manifest, &prepared->current,
		prepared->generation, prepared->transaction, material,
		anchor->digest);

	if (result != SLOT_ANCHOR_VALID)
		return result;
	if (!anchor_equal(anchor, &prepared->candidate) ||
	    !authorization_tuple_equal(prepared, &material->authorization))
		return SLOT_ANCHOR_INVALID;
	*authorized = true;
	return SLOT_ANCHOR_VALID;
}
#endif

static enum cb_err recover(struct journal_scan *scan)
{
	bool found = false;

	memset(scan, 0, sizeof(*scan));
	if (anchor_read(&scan->anchor) != CB_SUCCESS ||
	    anchor_cleared(&scan->anchor) || !scan->anchor.epoch)
		return CB_ERR;
	for (size_t domain = 0;
	     domain < PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS; domain++) {
		u32 slots = (u32)(journal.policy.port.layout.state[domain].size /
			journal.policy.port.layout.slot_size);

		for (uint32_t slot = 0; slot < slots; slot++) {
			struct payload_mm_fmp_owner_journal_manifest candidate;
			struct payload_mm_fmp_owner_journal_anchor candidate_anchor;
			u64 offset = journal.policy.port.layout.state[domain].offset +
				(uint64_t)slot * journal.policy.port.layout.slot_size;
			bool erased;
#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
			enum slot_anchor_result anchor_result;
			bool companion_erased;
			bool authorized;
			struct payload_mm_fmp_owner_prepared prepared;
			struct payload_mm_fmp_owner_transition_material material;
#endif

			if (slot_erased(offset, &erased) != CB_SUCCESS)
				return CB_ERR;
			if (erased)
				continue;
			scan->occupied[domain] = true;
			scan->last_occupied[domain] = slot;
			if (media_read(offset, &candidate, sizeof(candidate)) !=
				CB_SUCCESS || !manifest_shape_valid(&candidate))
				continue;
#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
			anchor_result = slot_anchor(offset,
				&candidate, &candidate_anchor, &prepared, &material,
				&companion_erased, &authorized);

			if (anchor_result == SLOT_ANCHOR_ERROR)
				return CB_ERR;
			if (anchor_result == SLOT_ANCHOR_INVALID)
				continue;
			if (!anchor_equal(&candidate_anchor, &scan->anchor))
				continue;
			if (
			    (!companion_erased &&
			     prepared.state != PAYLOAD_MM_FMP_OWNER_COMMITTED_STATE))
				continue;
#else
			candidate_anchor.epoch = candidate.epoch;
			if (manifest_digest(&candidate, candidate_anchor.digest) !=
				CB_SUCCESS ||
			    !anchor_equal(&candidate_anchor, &scan->anchor))
				continue;
#endif
			if (found && (memcmp(&scan->manifest, &candidate,
				sizeof(candidate)) != 0
#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
			    || scan->authorized != authorized ||
			    (authorized &&
			     (memcmp(&scan->prepared, &prepared, sizeof(prepared)) ||
			      memcmp(&scan->material, &material, sizeof(material))))
#endif
			    ))
				return CB_ERR;
			scan->manifest = candidate;
#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
			if (authorized) {
				scan->prepared = prepared;
				scan->material = material;
				scan->authorized = true;
			}
#endif
			scan->authoritative[domain] = true;
			found = true;
		}
	}
	return found ? CB_SUCCESS : CB_ERR;
}

#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
static enum cb_err find_prepared(
	struct payload_mm_fmp_owner_prepared *prepared,
	struct payload_mm_fmp_owner_journal_manifest *candidate,
	uint64_t *slot_offset, bool *present)
{
	bool found = false;

	*present = false;

	for (size_t domain = 0; domain < PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS;
	     domain++) {
		u32 slots = (u32)(journal.policy.port.layout.state[domain].size /
			journal.policy.port.layout.slot_size);

		for (uint32_t slot = 0; slot < slots; slot++) {
			struct payload_mm_fmp_owner_prepared record;
			struct payload_mm_fmp_owner_journal_manifest manifest;
			struct payload_mm_fmp_owner_journal_anchor digest;
			struct payload_mm_fmp_owner_transition_material material;
			uint64_t offset =
				journal.policy.port.layout.state[domain].offset +
				(uint64_t)slot * journal.policy.port.layout.slot_size;
			bool erased;
			bool authorized;

			if (prepared_read(offset, &record, &erased) != CB_SUCCESS)
				return CB_ERR;
			if (erased || !prepared_shape_valid(&record) ||
			    record.state != PAYLOAD_MM_FMP_OWNER_PREPARED_STATE)
				continue;
			if (media_read(offset, &manifest, sizeof(manifest)) != CB_SUCCESS ||
			    slot_anchor(offset, &manifest, &digest, &record, &material,
				&erased, &authorized) != SLOT_ANCHOR_VALID || erased)
				return CB_ERR;
			if (!anchor_equal(&digest, &record.candidate) || found)
				return CB_ERR;
			*prepared = record;
			*candidate = manifest;
			*slot_offset = offset;
			found = true;
		}
	}
	*present = found;
	return CB_SUCCESS;
}
#endif

static enum cb_err erase_domain(size_t domain)
{
	const struct fmp_owner_range *region =
		&journal.policy.port.layout.state[domain];
	bool erased;

	if (journal.poisoned)
		return CB_ERR;
	(void)journal.policy.port.erase(journal.policy.port.context, region->offset,
		region->size);
	if (!policy_unchanged() || slot_erased(region->offset, &erased) != CB_SUCCESS ||
	    !erased)
		return CB_ERR;
	/* Check every slot, not merely the first one. */
	for (u64 offset = region->offset +
		journal.policy.port.layout.slot_size;
	     offset < region->offset + region->size;
	     offset += journal.policy.port.layout.slot_size)
		if (slot_erased(offset, &erased) != CB_SUCCESS || !erased)
			return CB_ERR;
	return media_sync();
}

static enum cb_err write_manifest(size_t domain, uint32_t slot,
	const struct payload_mm_fmp_owner_journal_manifest *manifest)
{
	struct payload_mm_fmp_owner_journal_manifest verified;
	uint8_t expected_digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];
	uint8_t verified_digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];
	u64 offset = journal.policy.port.layout.state[domain].offset +
		(uint64_t)slot * journal.policy.port.layout.slot_size;
	bool erased;

	if (slot >= journal.policy.port.layout.state[domain].size /
		journal.policy.port.layout.slot_size ||
	    manifest_digest(manifest, expected_digest) != CB_SUCCESS ||
	    slot_erased(offset, &erased) != CB_SUCCESS || !erased ||
	    media_program(offset, manifest, sizeof(*manifest)) != CB_SUCCESS ||
	    media_read(offset, &verified, sizeof(verified)) != CB_SUCCESS ||
	    memcmp(manifest, &verified, sizeof(verified)) != 0 ||
	    manifest_digest(&verified, verified_digest) != CB_SUCCESS ||
	    memcmp(expected_digest, verified_digest, sizeof(expected_digest)) != 0 ||
	    media_sync() != CB_SUCCESS)
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err append_location(const struct journal_scan *scan,
	size_t *domain, uint32_t *slot)
{
	for (size_t i = 0; i < PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS; i++) {
		uint32_t next = scan->occupied[i] ? scan->last_occupied[i] + 1 : 0;
		u32 slots = (u32)(journal.policy.port.layout.state[i].size /
			journal.policy.port.layout.slot_size);

		if (scan->authoritative[i] && next < slots) {
			*domain = i;
			*slot = next;
			return CB_SUCCESS;
		}
	}

	/* Preserve an authoritative copy while preparing the other erase domain. */
	if (scan->authoritative[0] && !scan->authoritative[1])
		*domain = 1;
	else if (!scan->authoritative[0] && scan->authoritative[1])
		*domain = 0;
	else
		*domain = 0;
	if (erase_domain(*domain) != CB_SUCCESS ||
	    write_manifest(*domain, 0, &scan->manifest) != CB_SUCCESS)
		return CB_ERR;
#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
	if (scan->authorized) {
		uint64_t offset =
			journal.policy.port.layout.state[*domain].offset;

		if (write_transition_material(offset, &scan->material) != CB_SUCCESS ||
		    write_prepared(offset, &scan->prepared) != CB_SUCCESS)
			return CB_ERR;
	}
#endif

	/* With one original copy, reclaim it only after the replacement is durable. */
	if (scan->authoritative[0] != scan->authoritative[1]) {
		size_t old = *domain ^ 1U;

		if (erase_domain(old) != CB_SUCCESS)
			return CB_ERR;
	}
	*slot = 1;
	return CB_SUCCESS;
}

static enum cb_err advance_anchor(
	const struct payload_mm_fmp_owner_journal_anchor *current,
	const struct payload_mm_fmp_owner_journal_anchor *candidate)
{
	struct payload_mm_fmp_owner_journal_anchor current_copy = *current;
	struct payload_mm_fmp_owner_journal_anchor candidate_copy = *candidate;
	struct payload_mm_fmp_owner_journal_anchor verified;

	if (journal.poisoned)
		return CB_ERR;
	(void)journal.policy.port.anchor_advance(journal.policy.port.context,
		&current_copy, &candidate_copy);
	if (!policy_unchanged() || !anchor_equal(&current_copy, current) ||
	    !anchor_equal(&candidate_copy, candidate) ||
	    anchor_read(&verified) != CB_SUCCESS)
		return CB_ERR;
	return anchor_equal(&verified, candidate) ? CB_SUCCESS : CB_ERR;
}

static bool identity_matches(uint32_t key,
	const struct payload_mm_fmp_state_identity *identity)
{
	return key < PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS && identity &&
		!memcmp(identity, &journal.identity[key], sizeof(*identity));
}

static enum cb_err journal_read(const void *context,
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	struct payload_mm_fmp_owner_record *record)
{
	struct journal_scan scan;
	enum cb_err result = CB_ERR;

	(void)context;
	if (!journal.installed || journal.busy || journal.poisoned || !record ||
	    !identity_matches(key, identity) ||
	    journal_storage_overlaps(identity, sizeof(*identity)) ||
	    journal_storage_overlaps(record, sizeof(*record)) ||
	    payload_mm_authvar_buffers_overlap(identity, sizeof(*identity), record,
		sizeof(*record)))
		return CB_ERR;
	memset(record, 0, sizeof(*record));
	journal.busy = true;
	if (recover(&scan) == CB_SUCCESS) {
		*record = scan.manifest.record[key];
		result = CB_SUCCESS;
	}
	journal.busy = false;
	return result;
}

static enum cb_err journal_commit(const void *context,
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate)
{
	struct payload_mm_fmp_state_identity identity_snapshot;
	struct payload_mm_fmp_owner_record current_snapshot;
	struct payload_mm_fmp_owner_record candidate_snapshot;
	struct payload_mm_fmp_owner_journal_manifest next;
	struct payload_mm_fmp_owner_journal_anchor next_anchor;
	uint8_t verified_digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];
	struct journal_scan scan;
#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
	struct payload_mm_fmp_owner_prepared prepared;
	struct payload_mm_fmp_owner_journal_manifest prepared_manifest;
	uint64_t prepared_offset;
	bool prepared_present;
#endif
	size_t domain;
	uint32_t slot;
	enum cb_err result = CB_ERR;

	(void)context;
	if (!journal.installed || journal.busy || journal.poisoned || !current ||
	    !candidate ||
	    !identity_matches(key, identity) ||
	    journal_storage_overlaps(identity, sizeof(*identity)) ||
	    journal_storage_overlaps(current, sizeof(*current)) ||
	    journal_storage_overlaps(candidate, sizeof(*candidate)) ||
	    payload_mm_authvar_buffers_overlap(identity, sizeof(*identity), current,
		sizeof(*current)) ||
	    payload_mm_authvar_buffers_overlap(identity, sizeof(*identity), candidate,
		sizeof(*candidate)) ||
	    payload_mm_authvar_buffers_overlap(current, sizeof(*current), candidate,
		sizeof(*candidate)))
		return CB_ERR;
	identity_snapshot = *identity;
	current_snapshot = *current;
	candidate_snapshot = *candidate;
	if (!payload_mm_fmp_owner_record_valid(key, &current_snapshot) ||
	    !payload_mm_fmp_owner_record_valid(key, &candidate_snapshot))
		return CB_ERR;
	journal.busy = true;
#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
	if (find_prepared(&prepared, &prepared_manifest, &prepared_offset,
		&prepared_present) != CB_SUCCESS || prepared_present)
		goto out;
#endif
	if (recover(&scan) != CB_SUCCESS ||
#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
	    scan.authorized ||
#endif
	    scan.manifest.epoch == UINT64_MAX ||
	    memcmp(&scan.manifest.record[key], &current_snapshot,
		sizeof(current_snapshot)) != 0)
		goto out;
	next = scan.manifest;
	next.epoch++;
	next.record[key] = candidate_snapshot;
	if (manifest_digest(&next, next_anchor.digest) != CB_SUCCESS ||
	    append_location(&scan, &domain, &slot) != CB_SUCCESS ||
	    write_manifest(domain, slot, &next) != CB_SUCCESS ||
	    manifest_digest(&next, verified_digest) != CB_SUCCESS ||
	    memcmp(next_anchor.digest, verified_digest,
		sizeof(verified_digest)) != 0 ||
	    memcmp(identity, &identity_snapshot, sizeof(identity_snapshot)) != 0 ||
	    memcmp(current, &current_snapshot, sizeof(current_snapshot)) != 0 ||
	    memcmp(candidate, &candidate_snapshot,
		sizeof(candidate_snapshot)) != 0)
		goto out;
	next_anchor.epoch = next.epoch;
	if (advance_anchor(&scan.anchor, &next_anchor) != CB_SUCCESS)
		goto out;
	/* Reconcile an ambiguous advance against both protected anchor and media. */
	if (recover(&scan) != CB_SUCCESS ||
	    memcmp(&scan.manifest, &next, sizeof(next)) != 0)
		goto out;
	result = CB_SUCCESS;
out:
	journal.busy = false;
	return result;
}

#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
static enum cb_err manifest_present(
	const struct payload_mm_fmp_owner_journal_anchor *anchor)
{
	struct payload_mm_fmp_owner_journal_manifest found_manifest;
	struct payload_mm_fmp_owner_prepared found_prepared;
	struct payload_mm_fmp_owner_transition_material found_material;
	bool found_authorized = false;
	bool found = false;

	for (size_t domain = 0; domain < PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS;
	     domain++) {
		u32 slots = (u32)(journal.policy.port.layout.state[domain].size /
			journal.policy.port.layout.slot_size);

		for (uint32_t slot = 0; slot < slots; slot++) {
			struct payload_mm_fmp_owner_journal_manifest manifest;
			struct payload_mm_fmp_owner_journal_anchor candidate;
			struct payload_mm_fmp_owner_prepared prepared;
			struct payload_mm_fmp_owner_transition_material material;
			enum slot_anchor_result anchor_result;
			u64 offset = journal.policy.port.layout.state[domain].offset +
				(uint64_t)slot * journal.policy.port.layout.slot_size;
			bool erased;
			bool companion_erased;
			bool authorized;

			if (slot_erased(offset, &erased) != CB_SUCCESS)
				return CB_ERR;
			if (erased)
				continue;
			if (media_read(offset, &manifest, sizeof(manifest)) != CB_SUCCESS)
				return CB_ERR;
			if (!manifest_shape_valid(&manifest))
				continue;
			anchor_result = slot_anchor(offset,
				&manifest, &candidate, &prepared, &material,
				&companion_erased, &authorized);

			if (anchor_result == SLOT_ANCHOR_ERROR)
				return CB_ERR;
			if (anchor_result == SLOT_ANCHOR_INVALID)
				continue;
			if (!anchor_equal(anchor, &candidate))
				continue;
			if (found &&
			    (memcmp(&found_manifest, &manifest, sizeof(manifest)) ||
			     found_authorized != authorized ||
			     (authorized &&
			      (memcmp(&found_prepared, &prepared, sizeof(prepared)) ||
			       memcmp(&found_material, &material, sizeof(material))))))
				return CB_ERR;
			found_manifest = manifest;
			found_prepared = prepared;
			found_material = material;
			found_authorized = authorized;
			found = true;
		}
	}
	return found ? CB_SUCCESS : CB_ERR;
}

static enum cb_err write_prepared(uint64_t slot_offset,
	const struct payload_mm_fmp_owner_prepared *prepared)
{
	struct payload_mm_fmp_owner_prepared verified;
	uint64_t offset = slot_offset +
		sizeof(struct payload_mm_fmp_owner_journal_manifest);
	bool erased;

	if (prepared_read(slot_offset, &verified, &erased) != CB_SUCCESS || !erased ||
	    media_program(offset, prepared, sizeof(*prepared)) != CB_SUCCESS ||
	    prepared_read(slot_offset, &verified, &erased) != CB_SUCCESS || erased ||
	    memcmp(prepared, &verified, sizeof(verified)) != 0 ||
	    media_sync() != CB_SUCCESS)
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err write_transition_material(uint64_t slot_offset,
	const struct payload_mm_fmp_owner_transition_material *material)
{
	struct payload_mm_fmp_owner_transition_material verified;
	uint64_t offset = slot_offset +
		sizeof(struct payload_mm_fmp_owner_journal_manifest) +
		sizeof(struct payload_mm_fmp_owner_prepared);

	memset(&verified, 0, sizeof(verified));
	if (media_read(offset, &verified, sizeof(verified)) != CB_SUCCESS ||
	    !bytes_equal_value((const uint8_t *)&verified, sizeof(verified), 0xff) ||
	    media_program(offset, material, sizeof(*material)) != CB_SUCCESS ||
	    media_read(offset, &verified, sizeof(verified)) != CB_SUCCESS ||
	    memcmp(material, &verified, sizeof(verified)) ||
	    media_sync() != CB_SUCCESS)
		return CB_ERR;
	return CB_SUCCESS;
}

static bool prepare_inputs_unchanged(
	const struct payload_mm_fmp_state_identity *identity,
	const struct payload_mm_fmp_state_identity *identity_snapshot,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *current_snapshot,
	const struct payload_mm_fmp_owner_record *candidate,
	const struct payload_mm_fmp_owner_record *candidate_snapshot,
	const struct payload_mm_fmp_owner_transition_material *material,
	const struct payload_mm_fmp_owner_transition_material *material_snapshot)
{
	return !memcmp(identity, identity_snapshot, sizeof(*identity)) &&
		!memcmp(current, current_snapshot, sizeof(*current)) &&
		!memcmp(candidate, candidate_snapshot, sizeof(*candidate)) &&
		(!material || !memcmp(material, material_snapshot,
			sizeof(*material)));
}

static enum cb_err journal_prepare(
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate,
	uint64_t generation, uint64_t transaction,
	const struct payload_mm_fmp_owner_transition_material *material)
{
	struct payload_mm_fmp_state_identity identity_snapshot;
	struct payload_mm_fmp_owner_record current_snapshot;
	struct payload_mm_fmp_owner_record candidate_snapshot;
	struct payload_mm_fmp_owner_transition_material material_snapshot;
	struct payload_mm_fmp_owner_journal_manifest next;
	struct payload_mm_fmp_owner_prepared prepared;
	struct payload_mm_fmp_owner_journal_anchor next_anchor;
	struct payload_mm_fmp_owner_journal_manifest pending_manifest;
	struct journal_scan scan;
	uint64_t pending_offset;
	bool pending;
	size_t domain;
	uint32_t slot;
	uint8_t authorization_digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];
	enum cb_err result = CB_ERR;

	if (!journal.installed || journal.busy || journal.poisoned || !current ||
	    !candidate || (!material && (!generation || !transaction)) ||
	    journal.policy.port.layout.slot_size < sizeof(next) + sizeof(prepared) ||
	    (material && journal.policy.port.layout.slot_size <
		sizeof(next) + sizeof(prepared) + sizeof(*material)) ||
	    !identity_matches(key, identity) ||
	    journal_storage_overlaps(identity, sizeof(*identity)) ||
	    journal_storage_overlaps(current, sizeof(*current)) ||
	    journal_storage_overlaps(candidate, sizeof(*candidate)) ||
	    (material && journal_storage_overlaps(material, sizeof(*material))) ||
	    payload_mm_authvar_buffers_overlap(identity, sizeof(*identity), current,
		 sizeof(*current)) ||
	    payload_mm_authvar_buffers_overlap(identity, sizeof(*identity), candidate,
		 sizeof(*candidate)) ||
	    payload_mm_authvar_buffers_overlap(current, sizeof(*current), candidate,
		 sizeof(*candidate)) ||
	    (material &&
	     (payload_mm_authvar_buffers_overlap(identity, sizeof(*identity),
		 material, sizeof(*material)) ||
	      payload_mm_authvar_buffers_overlap(current, sizeof(*current),
		 material, sizeof(*material)) ||
	      payload_mm_authvar_buffers_overlap(candidate, sizeof(*candidate),
		 material, sizeof(*material)))))
		return CB_ERR;
	identity_snapshot = *identity;
	current_snapshot = *current;
	candidate_snapshot = *candidate;
	memset(&material_snapshot, 0, sizeof(material_snapshot));
	if (material) {
		material_snapshot = *material;
		generation = material_snapshot.authorization.generation;
		transaction = material_snapshot.authorization.transaction;
	}
	if (!payload_mm_fmp_owner_record_valid(key, &current_snapshot) ||
	    !payload_mm_fmp_owner_record_valid(key, &candidate_snapshot))
		return CB_ERR;
	journal.busy = true;
	if (material &&
	    (hash(&material_snapshot.authorization,
		sizeof(material_snapshot.authorization), authorization_digest) !=
		CB_SUCCESS ||
	     !payload_mm_fmp_owner_transition_material_valid(&material_snapshot,
		authorization_digest)))
		goto out;
	if (find_prepared(&prepared, &pending_manifest, &pending_offset,
		&pending) != CB_SUCCESS || pending || recover(&scan) != CB_SUCCESS ||
	    (scan.authorized && !material) ||
	    scan.manifest.epoch == UINT64_MAX ||
	    memcmp(&scan.manifest.record[key], &current_snapshot,
		sizeof(current_snapshot)) != 0)
		goto out;
	next = scan.manifest;
	next.epoch++;
	next.record[key] = candidate_snapshot;
	next_anchor.epoch = next.epoch;
	if ((material ? authorized_anchor_digest(&next, &scan.anchor, generation,
		transaction, &material_snapshot, next_anchor.digest) !=
			SLOT_ANCHOR_VALID :
		manifest_digest(&next, next_anchor.digest) != CB_SUCCESS) ||
	    append_location(&scan, &domain, &slot) != CB_SUCCESS ||
	    (material &&
	     (material_snapshot.authorization.generation != generation ||
	      material_snapshot.authorization.transaction != transaction ||
	      memcmp(&material_snapshot.authorization.current, &scan.anchor,
		sizeof(scan.anchor)) ||
	      memcmp(&material_snapshot.authorization.candidate, &next_anchor,
		sizeof(next_anchor)))) ||
	    !prepare_inputs_unchanged(identity, &identity_snapshot, current,
		&current_snapshot, candidate, &candidate_snapshot, material,
		&material_snapshot))
		goto out;
	pending_offset = journal.policy.port.layout.state[domain].offset +
		(uint64_t)slot * journal.policy.port.layout.slot_size;
	if (write_manifest(domain, slot, &next) != CB_SUCCESS)
		goto out;
	if (material && write_transition_material(pending_offset,
		&material_snapshot) != CB_SUCCESS)
		goto out;
	if (!prepare_inputs_unchanged(identity, &identity_snapshot, current,
		&current_snapshot, candidate, &candidate_snapshot, material,
		&material_snapshot))
		goto out;
	prepared = (struct payload_mm_fmp_owner_prepared) {
		.magic = PAYLOAD_MM_FMP_OWNER_PREPARED_MAGIC,
		.revision = PAYLOAD_MM_FMP_OWNER_PREPARED_REVISION,
		.size = sizeof(prepared),
		.state = PAYLOAD_MM_FMP_OWNER_PREPARED_STATE,
		.generation = generation,
		.transaction = transaction,
		.current = scan.anchor,
		.candidate = next_anchor,
	};
	if (write_prepared(pending_offset, &prepared) != CB_SUCCESS ||
	    !prepare_inputs_unchanged(identity, &identity_snapshot, current,
		&current_snapshot, candidate, &candidate_snapshot, material,
		&material_snapshot) ||
	    anchor_read(&next_anchor) != CB_SUCCESS ||
	    !anchor_equal(&next_anchor, &prepared.current))
		goto out;
	result = CB_SUCCESS;
out:
	journal.busy = false;
	return result;
}

enum cb_err payload_mm_fmp_owner_journal_prepare(
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate,
	uint64_t generation, uint64_t transaction)
{
	return journal_prepare(identity, key, current, candidate, generation,
		transaction, NULL);
}

enum cb_err payload_mm_fmp_owner_journal_prepare_authorized(
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate,
	const struct payload_mm_fmp_owner_transition_material *material)
{
	if (!material)
		return CB_ERR;
	return journal_prepare(identity, key, current, candidate, 0, 0, material);
}

enum cb_err payload_mm_fmp_owner_journal_reconcile_prepared(void)
{
	struct payload_mm_fmp_owner_prepared prepared;
	struct payload_mm_fmp_owner_prepared verified;
	struct payload_mm_fmp_owner_journal_manifest candidate;
	struct payload_mm_fmp_owner_journal_anchor anchor;
	struct capsule_tpm_anchor_value current;
	struct capsule_tpm_anchor_value next;
	struct journal_scan scan;
	uint64_t slot_offset;
	uint64_t state_offset;
	uint32_t committed = PAYLOAD_MM_FMP_OWNER_COMMITTED_STATE;
	bool erased;
	bool present;
	enum cb_err sync_status;
	enum cb_err result = CB_ERR;

	if (!journal.installed || journal.busy || journal.poisoned)
		return CB_ERR;
	journal.busy = true;
	if (find_prepared(&prepared, &candidate, &slot_offset, &present) !=
		CB_SUCCESS || !present ||
	    manifest_present(&prepared.current) != CB_SUCCESS)
		goto out;
	current.epoch = prepared.current.epoch;
	memcpy(current.digest, prepared.current.digest, sizeof(current.digest));
	next.epoch = prepared.candidate.epoch;
	memcpy(next.digest, prepared.candidate.digest, sizeof(next.digest));
	if (capsule_tpm_anchor_grant_consume(prepared.generation,
		prepared.transaction, &current, &next) != CB_SUCCESS ||
	    anchor_read(&anchor) != CB_SUCCESS ||
	    !anchor_equal(&anchor, &prepared.candidate))
		goto out;
	state_offset = slot_offset +
		offsetof(struct payload_mm_fmp_owner_prepared, state) +
		sizeof(struct payload_mm_fmp_owner_journal_manifest);
	(void)media_program(state_offset, &committed, sizeof(committed));
	sync_status = media_sync();
	if (sync_status != CB_SUCCESS) {
		journal.poisoned = true;
		goto out;
	}
	if (!policy_unchanged() ||
	    prepared_read(slot_offset, &verified, &erased) != CB_SUCCESS || erased ||
	    !prepared_shape_valid(&verified) ||
	    verified.state != PAYLOAD_MM_FMP_OWNER_COMMITTED_STATE ||
	    memcmp(&prepared, &verified,
		offsetof(struct payload_mm_fmp_owner_prepared, state)) != 0 ||
	    memcmp((const uint8_t *)&prepared +
		offsetof(struct payload_mm_fmp_owner_prepared, reserved),
		(const uint8_t *)&verified +
		offsetof(struct payload_mm_fmp_owner_prepared, reserved),
		sizeof(prepared) -
		offsetof(struct payload_mm_fmp_owner_prepared, reserved)) != 0 ||
	    recover(&scan) != CB_SUCCESS ||
	    memcmp(&scan.manifest, &candidate, sizeof(candidate)) != 0)
		goto out;
	result = CB_SUCCESS;
out:
	journal.busy = false;
	return result;
}
#endif

enum cb_err payload_mm_fmp_owner_journal_install(
	const struct payload_mm_fmp_owner_journal_port *trusted_port,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct payload_mm_fmp_owner_journal_port port;
	uint8_t context_snapshot[PAYLOAD_MM_FMP_OWNER_JOURNAL_CONTEXT_SIZE];
	bool protected;

	if (journal.install_attempted)
		return CB_ERR;
	journal.install_attempted = true;
	if (!payload_mm_fmp_state_authority_ready() || !trusted_port ||
	    !storage_is_protected)
		return CB_ERR;
	memcpy(&port, trusted_port, sizeof(port));
	if (port.revision != PAYLOAD_MM_FMP_OWNER_JOURNAL_REVISION ||
	    port.size != sizeof(port) ||
	    !payload_mm_fmp_layout_valid(&port.layout) ||
	    port.layout.slot_size <
		sizeof(struct payload_mm_fmp_owner_journal_manifest) ||
	    port.layout.slot_size % sizeof(uint64_t) ||
	    !port.read || !port.program || !port.erase ||
	    !port.sync || !port.sha256 || !port.anchor_read ||
	    !port.anchor_advance ||
	    ((port.context == NULL) != (port.context_size == 0)) ||
	    port.context_size > sizeof(context_snapshot))
		return CB_ERR;
	if (port.context_size)
		memcpy(context_snapshot, port.context, port.context_size);
	memset(&journal.policy, 0, sizeof(journal.policy));
	journal.policy.port = port;
	if (port.context_size) {
		memcpy(journal.policy.context, context_snapshot, port.context_size);
		journal.policy.port.context = journal.policy.context;
	}
	journal.sealed_policy = journal.policy;
	if (port.context_size)
		journal.sealed_policy.port.context = journal.sealed_policy.context;
	for (uint32_t key = 0; key < PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS; key++)
		if (payload_mm_fmp_state_identity_get_for_key(key,
			&journal.identity[key]) != CB_SUCCESS)
			return CB_ERR;
	protected = storage_is_protected(context, &journal, sizeof(journal));
	if (!protected || !policy_unchanged())
		return CB_ERR;
	journal.installed = true;
	journal.busy = true;
	if (bind_storage_domain() != CB_SUCCESS) {
		journal.busy = false;
		journal.installed = false;
		return CB_ERR;
	}
	if (hash(journal.identity, sizeof(journal.identity),
		journal.identity_binding) != CB_SUCCESS) {
		journal.busy = false;
		journal.installed = false;
		return CB_ERR;
	}
	journal.busy = false;
	return CB_SUCCESS;
}

enum cb_err payload_mm_fmp_owner_journal_backend(
	struct payload_mm_fmp_owner_backend *backend)
{
	if (!journal.installed || journal.busy || journal.poisoned || !backend ||
	    !payload_mm_fmp_state_staging_buffer(backend, sizeof(*backend)) ||
	    journal_storage_overlaps(backend, sizeof(*backend)))
		return CB_ERR;
	*backend = (struct payload_mm_fmp_owner_backend) {
		.revision = PAYLOAD_MM_FMP_OWNER_REVISION,
		.size = sizeof(*backend),
		.read = journal_read,
		.commit = journal_commit,
	};
	return CB_SUCCESS;
}

enum cb_err payload_mm_fmp_owner_journal_factory_provision(
	const struct payload_mm_fmp_owner_record
		record[PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS])
{
	struct payload_mm_fmp_owner_record
		record_snapshot[PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS];
	struct payload_mm_fmp_owner_journal_manifest manifest = {
		.magic = PAYLOAD_MM_FMP_OWNER_JOURNAL_MAGIC,
		.revision = PAYLOAD_MM_FMP_OWNER_JOURNAL_REVISION,
		.size = sizeof(manifest),
		.slot_size = journal.policy.port.layout.slot_size,
		.owner_record_size = sizeof(struct payload_mm_fmp_owner_record),
		.owner_record_count = PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS,
		.format = PAYLOAD_MM_FMP_OWNER_JOURNAL_FORMAT,
		.epoch = 1,
	};
	struct payload_mm_fmp_owner_journal_anchor anchor;
	struct payload_mm_fmp_owner_journal_anchor verified;
	uint8_t verified_digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];
	size_t storage_domain_size = sizeof(journal.storage_domain);
	enum cb_err result = CB_ERR;

	if (!journal.installed || journal.busy || journal.poisoned || !record ||
	    !journal.policy.port.anchor_provision ||
	    !payload_mm_fmp_state_staging_buffer(record,
		PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS * sizeof(*record)) ||
	    payload_mm_authvar_buffers_overlap(record,
		PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS * sizeof(*record), &journal,
		sizeof(journal)))
		return CB_ERR;
	memcpy(record_snapshot, record, sizeof(record_snapshot));
	for (uint32_t key = 0; key < PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS; key++)
		if (!payload_mm_fmp_owner_record_valid(key, &record_snapshot[key]))
			return CB_ERR;
	journal.busy = true;
	if (anchor_read(&anchor) != CB_SUCCESS || !anchor_cleared(&anchor) ||
	    memcmp(record, record_snapshot, sizeof(record_snapshot)) != 0)
		goto out;
	memcpy(manifest.storage_domain, journal.storage_domain, storage_domain_size);
	memcpy(manifest.identity_binding, journal.identity_binding,
		sizeof(manifest.identity_binding));
	memcpy(manifest.record, record_snapshot, sizeof(manifest.record));
	if (erase_domain(0) != CB_SUCCESS || erase_domain(1) != CB_SUCCESS ||
	    write_manifest(0, 0, &manifest) != CB_SUCCESS ||
	    manifest_digest(&manifest, anchor.digest) != CB_SUCCESS ||
	    manifest_digest(&manifest, verified_digest) != CB_SUCCESS ||
	    memcmp(anchor.digest, verified_digest, sizeof(verified_digest)) != 0 ||
	    memcmp(record, record_snapshot, sizeof(record_snapshot)) != 0)
		goto out;
	anchor.epoch = manifest.epoch;
	{
		struct payload_mm_fmp_owner_journal_anchor candidate = anchor;

		if (journal.poisoned)
			goto out;
		(void)journal.policy.port.anchor_provision(
			journal.policy.port.context, &candidate);
		if (!policy_unchanged() || !anchor_equal(&candidate, &anchor))
			goto out;
	}
	if (anchor_read(&verified) != CB_SUCCESS ||
	    !anchor_equal(&verified, &anchor))
		goto out;
	{
		struct journal_scan scan;

		if (recover(&scan) != CB_SUCCESS ||
		    memcmp(&scan.manifest, &manifest, sizeof(manifest)) != 0)
			goto out;
	}
	result = CB_SUCCESS;
out:
	journal.busy = false;
	return result;
}
