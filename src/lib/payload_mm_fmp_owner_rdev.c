/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>

#include "payload_mm_fmp_owner_rdev_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM FMP owner rdev adapter must only be built in SMM"
#endif

static bool buffers_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_start = (uintptr_t)left;
	uintptr_t right_start = (uintptr_t)right;

	if (!left_size || !right_size || left_start > UINTPTR_MAX - left_size ||
	    right_start > UINTPTR_MAX - right_size)
		return true;
	return left_start < right_start + right_size &&
		right_start < left_start + left_size;
}

static bool range_contains(const struct fmp_owner_range *range, u64 offset,
	size_t size, u64 *relative)
{
	u64 end;
	u64 range_end;

	if (!size || offset > UINT64_MAX - size ||
	    range->offset > UINT64_MAX - range->size)
		return false;
	end = offset + size;
	range_end = range->offset + range->size;
	if (offset < range->offset || end > range_end)
		return false;
	*relative = offset - range->offset;
	return true;
}

static bool policy_unchanged(struct payload_mm_fmp_owner_rdev *adapter)
{
	const struct region_device *root =
		adapter->sealed_policy.root_device;
	bool unchanged = !memcmp(&adapter->policy, &adapter->sealed_policy,
		sizeof(adapter->policy)) && root &&
		!memcmp(root,
			&adapter->sealed_policy.root,
			sizeof(adapter->sealed_policy.root)) && root->ops &&
		!memcmp(root->ops, &adapter->sealed_policy.ops,
			sizeof(adapter->sealed_policy.ops));

	if (!unchanged)
		adapter->poisoned = true;
	return unchanged;
}

static bool context_matches(
	const struct payload_mm_fmp_owner_rdev_context *context,
	const struct payload_mm_fmp_owner_rdev *adapter);

static bool boundary_valid(const void *opaque,
	struct payload_mm_fmp_owner_rdev *adapter)
{
	bool valid = adapter->initialized && adapter->busy &&
		!adapter->poisoned && policy_unchanged(adapter) &&
		context_matches(opaque, adapter);

	if (!valid)
		adapter->poisoned = true;
	return valid;
}

static bool context_matches(
	const struct payload_mm_fmp_owner_rdev_context *context,
	const struct payload_mm_fmp_owner_rdev *adapter)
{
	const struct fmp_owner_layout *layout = &adapter->sealed_policy.layout;

	return context->revision == PAYLOAD_MM_FMP_OWNER_RDEV_REVISION &&
		context->size == sizeof(*context) &&
		context->adapter == adapter &&
		context->media_size == layout->media_size &&
		context->erase_size == layout->erase_size &&
		context->slot_size == layout->slot_size &&
		!memcmp(context->state, layout->state, sizeof(context->state));
}

static enum cb_err enter(const void *opaque,
	struct payload_mm_fmp_owner_rdev **adapter_out)
{
	const struct payload_mm_fmp_owner_rdev_context *context = opaque;
	struct payload_mm_fmp_owner_rdev *adapter;

	if (!context || context->revision != PAYLOAD_MM_FMP_OWNER_RDEV_REVISION ||
	    context->size != sizeof(*context) || !context->adapter)
		return CB_ERR;
	adapter = context->adapter;
	if (adapter->busy)
		return CB_ERR;
	if (!adapter->initialized || adapter->poisoned ||
	    !policy_unchanged(adapter) || !context_matches(context, adapter))
		return CB_ERR;
	adapter->busy = true;
	if (!boundary_valid(opaque, adapter)) {
		adapter->busy = false;
		return CB_ERR;
	}
	*adapter_out = adapter;
	return CB_SUCCESS;
}

static enum cb_err leave(const void *opaque,
	struct payload_mm_fmp_owner_rdev *adapter, enum cb_err result)
{
	if (!boundary_valid(opaque, adapter))
		result = CB_ERR;
	adapter->busy = false;
	return result;
}

static enum cb_err locate(struct payload_mm_fmp_owner_rdev *adapter,
	u64 offset, size_t size)
{
	for (size_t domain = 0; domain < PAYLOAD_MM_FMP_OWNER_LAYOUT_DOMAINS;
	     domain++) {
		u64 translated;

		if (!range_contains(&adapter->policy.layout.state[domain], offset,
			size, &translated))
			continue;
		if (translated > SIZE_MAX)
			return CB_ERR;
		return CB_SUCCESS;
	}
	return CB_ERR;
}

static ssize_t media_read(const void *opaque,
	struct payload_mm_fmp_owner_rdev *adapter, void *buffer, u64 offset,
	size_t size)
{
	ssize_t result;

	if (!boundary_valid(opaque, adapter) || offset > SIZE_MAX)
		return -1;
	result = adapter->sealed_policy.ops.readat(
		adapter->sealed_policy.root_device, buffer, (size_t)offset, size);
	return boundary_valid(opaque, adapter) ? result : -1;
}

static ssize_t media_write(const void *opaque,
	struct payload_mm_fmp_owner_rdev *adapter, const void *buffer, u64 offset,
	size_t size)
{
	ssize_t result;

	if (!boundary_valid(opaque, adapter) || offset > SIZE_MAX)
		return -1;
	result = adapter->sealed_policy.ops.writeat(
		adapter->sealed_policy.root_device, buffer, (size_t)offset, size);
	return boundary_valid(opaque, adapter) ? result : -1;
}

static ssize_t media_erase(const void *opaque,
	struct payload_mm_fmp_owner_rdev *adapter, u64 offset, size_t size)
{
	ssize_t result;

	if (!boundary_valid(opaque, adapter) || offset > SIZE_MAX)
		return -1;
	result = adapter->sealed_policy.ops.eraseat(
		adapter->sealed_policy.root_device, (size_t)offset, size);
	return boundary_valid(opaque, adapter) ? result : -1;
}

enum cb_err payload_mm_fmp_owner_rdev_read(const void *opaque, u64 offset,
	void *buffer, size_t size)
{
	struct payload_mm_fmp_owner_rdev *adapter;
	enum cb_err result = CB_ERR;

	if (!buffer || enter(opaque, &adapter) != CB_SUCCESS)
		return CB_ERR;
	if (buffers_overlap(buffer, size, adapter, sizeof(*adapter)) ||
	    buffers_overlap(buffer, size, opaque,
		sizeof(struct payload_mm_fmp_owner_rdev_context)))
		return leave(opaque, adapter, CB_ERR);
	if (locate(adapter, offset, size) == CB_SUCCESS &&
	    media_read(opaque, adapter, buffer, offset, size) == (ssize_t)size)
		result = CB_SUCCESS;
	return leave(opaque, adapter, result);
}

enum cb_err payload_mm_fmp_owner_rdev_program(const void *opaque, u64 offset,
	const void *buffer, size_t size)
{
	u8 before[sizeof(struct payload_mm_fmp_owner_journal_manifest)];
	u8 expected[sizeof(before)];
	u8 program_data[sizeof(before)];
	u8 after[sizeof(before)];
	struct payload_mm_fmp_owner_rdev *adapter;
	enum cb_err result = CB_ERR;

	if (!buffer || !size || size > sizeof(before) ||
	    enter(opaque, &adapter) != CB_SUCCESS)
		return CB_ERR;
	if (buffers_overlap(buffer, size, adapter, sizeof(*adapter)) ||
	    buffers_overlap(buffer, size, opaque,
		sizeof(struct payload_mm_fmp_owner_rdev_context)))
		goto out;
	memcpy(expected, buffer, size);
	memcpy(program_data, expected, size);
	if (locate(adapter, offset, size) != CB_SUCCESS ||
	    media_read(opaque, adapter, before, offset, size) != (ssize_t)size)
		goto out;
	for (size_t i = 0; i < size; i++)
		if ((before[i] & expected[i]) != expected[i])
			goto out;

	/* Always reconcile short, failed, or otherwise ambiguous completion. */
	(void)media_write(opaque, adapter, program_data, offset, size);
	if (!boundary_valid(opaque, adapter))
		goto out;
	if (media_read(opaque, adapter, after, offset, size) == (ssize_t)size &&
	    !memcmp(after, expected, size) &&
	    !memcmp(program_data, expected, size) &&
	    !memcmp(buffer, expected, size))
		result = CB_SUCCESS;
out:
	return leave(opaque, adapter, result);
}

enum cb_err payload_mm_fmp_owner_rdev_erase(const void *opaque, u64 offset,
	size_t size)
{
	u8 verify[64];
	struct payload_mm_fmp_owner_rdev *adapter;
	size_t done = 0;
	enum cb_err result = CB_ERR;

	if (enter(opaque, &adapter) != CB_SUCCESS)
		return CB_ERR;
	if (!size || offset % adapter->policy.layout.erase_size ||
	    size % adapter->policy.layout.erase_size ||
	    locate(adapter, offset, size) != CB_SUCCESS)
		goto out;
	(void)media_erase(opaque, adapter, offset, size);
	if (!boundary_valid(opaque, adapter))
		goto out;
	while (done < size) {
		size_t chunk = MIN(sizeof(verify), size - done);

		if (media_read(opaque, adapter, verify, offset + done, chunk) !=
		    (ssize_t)chunk)
			goto out;
		for (size_t i = 0; i < chunk; i++)
			if (verify[i] != 0xff)
				goto out;
		done += chunk;
	}
	result = CB_SUCCESS;
out:
	return leave(opaque, adapter, result);
}

enum cb_err payload_mm_fmp_owner_rdev_sync(const void *opaque)
{
	struct payload_mm_fmp_owner_rdev *adapter;
	enum cb_err result;

	if (enter(opaque, &adapter) != CB_SUCCESS)
		return CB_ERR;
	if (!boundary_valid(opaque, adapter))
		return leave(opaque, adapter, CB_ERR);
	result = adapter->sealed_policy.sync(adapter->policy.sync_context);
	if (!boundary_valid(opaque, adapter))
		result = CB_ERR;
	return leave(opaque, adapter,
		result == CB_SUCCESS ? CB_SUCCESS : CB_ERR);
}

enum cb_err payload_mm_fmp_owner_rdev_init(
	struct payload_mm_fmp_owner_rdev *adapter,
	struct payload_mm_fmp_owner_rdev_context *context,
	const struct fmp_owner_layout *layout,
	const struct region_device state[PAYLOAD_MM_FMP_OWNER_LAYOUT_DOMAINS],
	payload_mm_fmp_owner_rdev_sync_fn *sync, const void *sync_context,
	size_t sync_context_size)
{
	struct payload_mm_fmp_owner_rdev_policy policy;

	if (!adapter)
		return CB_ERR;
	if (adapter->initialized || adapter->busy || adapter->poisoned) {
		adapter->poisoned = true;
		return CB_ERR;
	}
	/* Every attempted initialization is one-shot, including malformed input. */
	adapter->poisoned = true;
	if (!context || !layout || !state || !sync ||
	    ((sync_context == NULL) != (sync_context_size == 0)) ||
	    sync_context_size > PAYLOAD_MM_FMP_OWNER_RDEV_SYNC_CONTEXT_SIZE ||
	    !payload_mm_fmp_layout_valid(layout))
		return CB_ERR;
	memset(&policy, 0, sizeof(policy));
	policy.revision = PAYLOAD_MM_FMP_OWNER_RDEV_REVISION;
	policy.size = sizeof(policy);
	policy.layout = *layout;
	policy.sync = sync;
	policy.sync_context_size = sync_context_size;
	if (sync_context_size)
		memcpy(policy.sync_context, sync_context, sync_context_size);
	for (size_t domain = 0; domain < PAYLOAD_MM_FMP_OWNER_LAYOUT_DOMAINS;
	     domain++) {
		if (!state[domain].root || !state[domain].root->ops ||
		    !state[domain].root->ops->readat ||
		    !state[domain].root->ops->writeat ||
		    !state[domain].root->ops->eraseat ||
		    region_device_offset(&state[domain]) !=
			layout->state[domain].offset ||
		    region_device_sz(&state[domain]) != layout->state[domain].size)
			return CB_ERR;
		policy.state[domain] = state[domain];
	}
	if (policy.state[0].root != policy.state[1].root)
		return CB_ERR;
	if (policy.state[0].root->root ||
	    region_device_offset(policy.state[0].root) != 0 ||
	    region_device_sz(policy.state[0].root) != layout->media_size)
		return CB_ERR;
	policy.root_device = policy.state[0].root;
	policy.root = *policy.state[0].root;
	policy.ops = *policy.root.ops;

	memset(adapter, 0, sizeof(*adapter));
	adapter->policy = policy;
	adapter->sealed_policy = policy;
	adapter->initialized = true;
	*context = (struct payload_mm_fmp_owner_rdev_context) {
		.revision = PAYLOAD_MM_FMP_OWNER_RDEV_REVISION,
		.size = sizeof(*context),
		.adapter = adapter,
		.media_size = layout->media_size,
		.erase_size = layout->erase_size,
		.slot_size = layout->slot_size,
		.state = { layout->state[0], layout->state[1] },
	};
	return CB_SUCCESS;
}
