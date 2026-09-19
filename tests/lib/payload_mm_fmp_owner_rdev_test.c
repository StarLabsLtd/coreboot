/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "payload_mm_fmp_owner_rdev_internal.h"

#define MEDIA_SIZE 16384U
#define ERASE_SIZE 512U
#define STATE_SIZE 1024U

enum fault {
	FAULT_NONE,
	FAULT_SHORT,
	FAULT_ERROR,
	FAULT_PARTIAL,
	FAULT_AMBIGUOUS,
	FAULT_MUTATE_POLICY,
	FAULT_MUTATE_INPUT,
	FAULT_MUTATE_ROOT,
	FAULT_MUTATE_STATE,
	FAULT_MUTATE_OPS,
	FAULT_MUTATE_CONTEXT,
	FAULT_REINIT,
	FAULT_REENTER,
};

struct media_model {
	struct region_device rdev;
	u8 bytes[MEDIA_SIZE];
	enum fault read_fault;
	enum fault write_fault;
	enum fault erase_fault;
	enum fault sync_fault;
	unsigned int reads;
	unsigned int writes;
	unsigned int erases;
	unsigned int syncs;
};

struct sync_context {
	struct media_model *model;
	u32 tag;
};

static struct media_model media;
static struct payload_mm_fmp_owner_rdev adapter;
static struct payload_mm_fmp_owner_rdev_context adapter_context;
static struct region_device_ops media_ops;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

void region_device_init(struct region_device *rdev,
	const struct region_device_ops *ops, size_t offset, size_t size)
{
	*rdev = (struct region_device) {
		.ops = ops,
		.region = { .offset = offset, .size = size },
	};
}

int rdev_chain(struct region_device *child, const struct region_device *parent,
	size_t offset, size_t size)
{
	if (offset > parent->region.size || size > parent->region.size - offset)
		return -1;
	*child = (struct region_device) {
		.root = parent->root ? parent->root : parent,
		.region = {
			.offset = parent->region.offset + offset,
			.size = size,
		},
	};
	return 0;
}

ssize_t rdev_readat(const struct region_device *rdev, void *buffer,
	size_t offset, size_t size)
{
	if (offset > rdev->region.size || size > rdev->region.size - offset)
		return -1;
	return rdev->root->ops->readat(rdev->root, buffer,
		rdev->region.offset + offset, size);
}

ssize_t rdev_writeat(const struct region_device *rdev, const void *buffer,
	size_t offset, size_t size)
{
	if (offset > rdev->region.size || size > rdev->region.size - offset)
		return -1;
	return rdev->root->ops->writeat(rdev->root, buffer,
		rdev->region.offset + offset, size);
}

ssize_t rdev_eraseat(const struct region_device *rdev, size_t offset,
	size_t size)
{
	if (offset > rdev->region.size || size > rdev->region.size - offset)
		return -1;
	return rdev->root->ops->eraseat(rdev->root,
		rdev->region.offset + offset, size);
}

static struct media_model *model(const struct region_device *rdev)
{
	return container_of(rdev, struct media_model, rdev);
}

static void mutate_boundary(enum fault fault, struct media_model *state)
{
	switch (fault) {
	case FAULT_MUTATE_POLICY:
		adapter.policy.layout.slot_size ^= 1;
		break;
	case FAULT_MUTATE_ROOT:
		state->rdev.region.size--;
		break;
	case FAULT_MUTATE_STATE:
		adapter.policy.state[0].region.size--;
		break;
	case FAULT_MUTATE_OPS:
		media_ops.readat = NULL;
		break;
	case FAULT_MUTATE_CONTEXT:
		adapter_context.erase_size ^= 1;
		break;
	case FAULT_REINIT:
		assert(payload_mm_fmp_owner_rdev_init(&adapter, &adapter_context,
			NULL, NULL, NULL, NULL, 0) == CB_ERR);
		assert(adapter.poisoned);
		assert(payload_mm_fmp_owner_rdev_sync(&adapter_context) == CB_ERR);
		break;
	default:
		break;
	}
}

static ssize_t model_read(const struct region_device *rdev, void *buffer,
	size_t offset, size_t size)
{
	struct media_model *state = model(rdev);

	state->reads++;
	if (state->read_fault == FAULT_REENTER)
		assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
			buffer, 1) == CB_ERR);
	mutate_boundary(state->read_fault, state);
	if (state->read_fault == FAULT_ERROR)
		return -1;
	if (state->read_fault == FAULT_SHORT) {
		memcpy(buffer, state->bytes + offset, size - 1);
		return (ssize_t)size - 1;
	}
	memcpy(buffer, state->bytes + offset, size);
	return (ssize_t)size;
}

static ssize_t model_write(const struct region_device *rdev,
	const void *buffer, size_t offset, size_t size)
{
	struct media_model *state = model(rdev);
	size_t written = size;

	state->writes++;
	if (state->write_fault == FAULT_REENTER)
		assert(payload_mm_fmp_owner_rdev_sync(&adapter_context) == CB_ERR);
	mutate_boundary(state->write_fault, state);
	if (state->write_fault == FAULT_ERROR)
		return -1;
	if (state->write_fault == FAULT_PARTIAL ||
	    state->write_fault == FAULT_SHORT)
		written /= 2;
	for (size_t i = 0; i < written; i++)
		state->bytes[offset + i] &= ((const u8 *)buffer)[i];
	if (state->write_fault == FAULT_MUTATE_INPUT)
		((u8 *)buffer)[0] ^= 1;
	if (state->write_fault == FAULT_AMBIGUOUS)
		return -1;
	return state->write_fault == FAULT_SHORT ? (ssize_t)written :
		(ssize_t)size;
}

static ssize_t model_erase(const struct region_device *rdev, size_t offset,
	size_t size)
{
	struct media_model *state = model(rdev);
	size_t erased = size;

	state->erases++;
	if (state->erase_fault == FAULT_REENTER)
		assert(payload_mm_fmp_owner_rdev_erase(&adapter_context, 0x1000,
			ERASE_SIZE) == CB_ERR);
	mutate_boundary(state->erase_fault, state);
	if (state->erase_fault == FAULT_ERROR)
		return -1;
	if (state->erase_fault == FAULT_PARTIAL ||
	    state->erase_fault == FAULT_SHORT)
		erased /= 2;
	memset(state->bytes + offset, 0xff, erased);
	if (state->erase_fault == FAULT_AMBIGUOUS)
		return -1;
	return state->erase_fault == FAULT_SHORT ? (ssize_t)erased :
		(ssize_t)size;
}

static const struct region_device_ops media_ops_template = {
	.readat = model_read,
	.writeat = model_write,
	.eraseat = model_erase,
};

static enum cb_err model_sync(const void *opaque)
{
	struct sync_context *context = (void *)opaque;
	struct media_model *state = context->model;

	assert(context->tag == 0x52444556U);
	state->syncs++;
	if (state->sync_fault == FAULT_REENTER)
		assert(payload_mm_fmp_owner_rdev_sync(&adapter_context) == CB_ERR);
	mutate_boundary(state->sync_fault, state);
	if (state->sync_fault == FAULT_MUTATE_INPUT)
		context->tag = 0;
	return state->sync_fault == FAULT_ERROR ? CB_ERR : CB_SUCCESS;
}

static struct fmp_owner_layout layout(void)
{
	return (struct fmp_owner_layout) {
		.revision = PAYLOAD_MM_FMP_OWNER_LAYOUT_REVISION,
		.size = sizeof(struct fmp_owner_layout),
		.media_size = MEDIA_SIZE,
		.erase_size = ERASE_SIZE,
		.slot_size = ERASE_SIZE,
		.route_count = 1,
		.state = {
			{ .offset = 0x1000, .size = STATE_SIZE },
			{ .offset = 0x1800, .size = STATE_SIZE },
		},
		.smmstore = { .offset = 0x2000, .size = ERASE_SIZE },
		.route = { {
			.image_offset = 0,
			.flash_offset = 0x3000,
			.size = ERASE_SIZE,
			.flags = LB_CAPSULE_REGION_BIOS,
		} },
	};
}

static void reset_adapter(void)
{
	struct fmp_owner_layout descriptor = layout();
	struct region_device state[2];
	struct sync_context sync = {
		.model = &media,
		.tag = 0x52444556U,
	};

	memset(&media, 0, sizeof(media));
	memset(&adapter, 0, sizeof(adapter));
	memset(&adapter_context, 0, sizeof(adapter_context));
	media_ops = media_ops_template;
	memset(media.bytes, 0xff, sizeof(media.bytes));
	region_device_init(&media.rdev, &media_ops, 0, sizeof(media.bytes));
	assert(rdev_chain(&state[0], &media.rdev, descriptor.state[0].offset,
		descriptor.state[0].size) == 0);
	assert(rdev_chain(&state[1], &media.rdev, descriptor.state[1].offset,
		descriptor.state[1].size) == 0);
	assert(payload_mm_fmp_owner_rdev_init(&adapter, &adapter_context,
		&descriptor, state, model_sync, &sync, sizeof(sync)) == CB_SUCCESS);
	/* Caller-owned descriptors and context are not retained. */
	memset(&descriptor, 0, sizeof(descriptor));
	memset(state, 0, sizeof(state));
	memset(&sync, 0, sizeof(sync));
}

static void test_happy_path(void)
{
	u8 written[64];
	u8 readback[64];

	reset_adapter();
	memset(written, 0xa5, sizeof(written));
	assert(payload_mm_fmp_owner_rdev_program(&adapter_context, 0x1000,
		written, sizeof(written)) == CB_SUCCESS);
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
		readback, sizeof(readback)) == CB_SUCCESS);
	assert(!memcmp(readback, written, sizeof(written)));
	assert(payload_mm_fmp_owner_rdev_sync(&adapter_context) == CB_SUCCESS);
	assert(media.syncs == 1);
	assert(payload_mm_fmp_owner_rdev_erase(&adapter_context, 0x1000,
		ERASE_SIZE) == CB_SUCCESS);
	for (size_t i = 0x1000; i < 0x1000 + ERASE_SIZE; i++)
		assert(media.bytes[i] == 0xff);
}

static void test_bounds_and_geometry(void)
{
	u8 data[64] = { 0 };

	reset_adapter();
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x0fff,
		data, 1) == CB_ERR);
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x13f0,
		data, 32) == CB_ERR);
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1400,
		data, 1) == CB_ERR);
	assert(payload_mm_fmp_owner_rdev_program(&adapter_context, 0x13f0,
		data, 32) == CB_ERR);
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
		data, 0) == CB_ERR);
	assert(payload_mm_fmp_owner_rdev_erase(&adapter_context, 0x1001,
		ERASE_SIZE) == CB_ERR);
	assert(payload_mm_fmp_owner_rdev_erase(&adapter_context, 0x1000,
		ERASE_SIZE / 2) == CB_ERR);
	assert(media.erases == 0);
}

static void test_program_semantics_and_completion(void)
{
	u8 data[64];

	reset_adapter();
	memset(data, 0x55, sizeof(data));
	media.write_fault = FAULT_AMBIGUOUS;
	assert(payload_mm_fmp_owner_rdev_program(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_SUCCESS);
	media.write_fault = FAULT_NONE;
	memset(data, 0xff, sizeof(data));
	assert(payload_mm_fmp_owner_rdev_program(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_ERR);

	for (enum fault fault = FAULT_SHORT; fault <= FAULT_PARTIAL; fault++) {
		reset_adapter();
		memset(data, 0, sizeof(data));
		media.write_fault = fault;
		assert(payload_mm_fmp_owner_rdev_program(&adapter_context, 0x1000,
			data, sizeof(data)) == CB_ERR);
	}
	reset_adapter();
	memset(data, 0, sizeof(data));
	media.write_fault = FAULT_MUTATE_INPUT;
	assert(payload_mm_fmp_owner_rdev_program(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_ERR);
}

static void test_erase_completion(void)
{
	for (enum fault fault = FAULT_SHORT; fault <= FAULT_PARTIAL; fault++) {
		reset_adapter();
		memset(media.bytes + 0x1000, 0, ERASE_SIZE);
		media.erase_fault = fault;
		assert(payload_mm_fmp_owner_rdev_erase(&adapter_context, 0x1000,
			ERASE_SIZE) == CB_ERR);
	}
	reset_adapter();
	memset(media.bytes + 0x1000, 0, ERASE_SIZE);
	media.erase_fault = FAULT_AMBIGUOUS;
	assert(payload_mm_fmp_owner_rdev_erase(&adapter_context, 0x1000,
		ERASE_SIZE) == CB_SUCCESS);
}

static void test_short_io_and_sync_error(void)
{
	u8 data[16];

	reset_adapter();
	media.read_fault = FAULT_SHORT;
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_ERR);
	reset_adapter();
	media.read_fault = FAULT_ERROR;
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_ERR);
	reset_adapter();
	media.sync_fault = FAULT_ERROR;
	assert(payload_mm_fmp_owner_rdev_sync(&adapter_context) == CB_ERR);

	reset_adapter();
	media.read_fault = FAULT_MUTATE_ROOT;
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_ERR);
	assert(adapter.poisoned);
}

static void test_mutation_and_reentry(void)
{
	u8 data[16];

	reset_adapter();
	media.read_fault = FAULT_REENTER;
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_SUCCESS);
	reset_adapter();
	media.read_fault = FAULT_MUTATE_POLICY;
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_ERR);
	assert(adapter.poisoned);
	assert(payload_mm_fmp_owner_rdev_sync(&adapter_context) == CB_ERR);

	reset_adapter();
	media.sync_fault = FAULT_MUTATE_POLICY;
	assert(payload_mm_fmp_owner_rdev_sync(&adapter_context) == CB_ERR);
	assert(adapter.poisoned);

	reset_adapter();
	adapter_context.erase_size ^= 1;
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_ERR);
}

static void test_boundary_mutation(enum fault fault)
{
	struct payload_mm_fmp_owner_rdev_policy saved_policy;
	struct payload_mm_fmp_owner_rdev_context saved_context;
	u8 data[64] = { 0 };

	reset_adapter();
	media.read_fault = fault;
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_ERR);
	assert(adapter.poisoned);

	reset_adapter();
	media.read_fault = fault;
	assert(payload_mm_fmp_owner_rdev_program(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_ERR);
	assert(media.reads == 1 && media.writes == 0);
	assert(adapter.poisoned);

	reset_adapter();
	media.write_fault = fault;
	assert(payload_mm_fmp_owner_rdev_program(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_ERR);
	assert(media.reads == 1 && media.writes == 1);
	assert(adapter.poisoned);

	reset_adapter();
	memset(media.bytes + 0x1000, 0, ERASE_SIZE);
	media.erase_fault = fault;
	assert(payload_mm_fmp_owner_rdev_erase(&adapter_context, 0x1000,
		ERASE_SIZE) == CB_ERR);
	assert(media.erases == 1 && media.reads == 0);
	assert(adapter.poisoned);

	reset_adapter();
	media.sync_fault = fault;
	assert(payload_mm_fmp_owner_rdev_sync(&adapter_context) == CB_ERR);
	assert(media.syncs == 1);
	assert(adapter.poisoned);

	if (fault != FAULT_REINIT)
		return;
	reset_adapter();
	saved_policy = adapter.policy;
	saved_context = adapter_context;
	media.read_fault = FAULT_REINIT;
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_ERR);
	assert(!memcmp(&adapter.policy, &saved_policy, sizeof(saved_policy)));
	assert(!memcmp(&adapter_context, &saved_context,
		sizeof(saved_context)));
	assert(adapter.initialized && adapter.poisoned);
	assert(payload_mm_fmp_owner_rdev_sync(&adapter_context) == CB_ERR);
}

static void test_nested_operations(void)
{
	u8 data[64] = { 0 };

	reset_adapter();
	media.read_fault = FAULT_REENTER;
	assert(payload_mm_fmp_owner_rdev_read(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_SUCCESS);
	assert(!adapter.poisoned);

	reset_adapter();
	media.write_fault = FAULT_REENTER;
	assert(payload_mm_fmp_owner_rdev_program(&adapter_context, 0x1000,
		data, sizeof(data)) == CB_SUCCESS);
	assert(!adapter.poisoned);

	reset_adapter();
	memset(media.bytes + 0x1000, 0, ERASE_SIZE);
	media.erase_fault = FAULT_REENTER;
	assert(payload_mm_fmp_owner_rdev_erase(&adapter_context, 0x1000,
		ERASE_SIZE) == CB_SUCCESS);
	assert(!adapter.poisoned);

	reset_adapter();
	media.sync_fault = FAULT_REENTER;
	assert(payload_mm_fmp_owner_rdev_sync(&adapter_context) == CB_SUCCESS);
	assert(!adapter.poisoned);
}

static void test_init_rejects_incomplete_authority(void)
{
	struct fmp_owner_layout descriptor = layout();
	struct payload_mm_fmp_owner_rdev temporary_adapter;
	struct payload_mm_fmp_owner_rdev_context context;
	struct region_device state[2];
	struct region_device_ops missing_erase;

	memset(&media, 0, sizeof(media));
	media_ops = media_ops_template;
	missing_erase = media_ops;
	region_device_init(&media.rdev, &media_ops, 0, sizeof(media.bytes));
	assert(rdev_chain(&state[0], &media.rdev, descriptor.state[0].offset,
		descriptor.state[0].size) == 0);
	assert(rdev_chain(&state[1], &media.rdev, descriptor.state[1].offset,
		descriptor.state[1].size) == 0);
	memset(&temporary_adapter, 0, sizeof(temporary_adapter));
	assert(payload_mm_fmp_owner_rdev_init(&temporary_adapter, &context,
		&descriptor, state, NULL, NULL, 0) == CB_ERR);
	assert(temporary_adapter.poisoned);
	state[0].region.size--;
	memset(&temporary_adapter, 0, sizeof(temporary_adapter));
	assert(payload_mm_fmp_owner_rdev_init(&temporary_adapter, &context,
		&descriptor, state, model_sync, NULL, 0) == CB_ERR);
	assert(temporary_adapter.poisoned);
	state[0].region.size++;
	missing_erase.eraseat = NULL;
	media.rdev.ops = &missing_erase;
	state[0].root = &media.rdev;
	state[1].root = &media.rdev;
	memset(&temporary_adapter, 0, sizeof(temporary_adapter));
	assert(payload_mm_fmp_owner_rdev_init(&temporary_adapter, &context,
		&descriptor, state, model_sync, NULL, 0) == CB_ERR);
	assert(temporary_adapter.poisoned);
}

int main(void)
{
	test_happy_path();
	test_bounds_and_geometry();
	test_program_semantics_and_completion();
	test_erase_completion();
	test_short_io_and_sync_error();
	test_mutation_and_reentry();
	test_boundary_mutation(FAULT_MUTATE_POLICY);
	test_boundary_mutation(FAULT_MUTATE_ROOT);
	test_boundary_mutation(FAULT_MUTATE_STATE);
	test_boundary_mutation(FAULT_MUTATE_OPS);
	test_boundary_mutation(FAULT_MUTATE_CONTEXT);
	test_boundary_mutation(FAULT_REINIT);
	test_nested_operations();
	test_init_rejects_incomplete_authority();
	return 0;
}
