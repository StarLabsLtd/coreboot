/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifndef BACKEND_SOURCE_INCLUDE
#define BACKEND_SOURCE_INCLUDE "../../src/lib/capsule_qemu_pflash.c"
#endif
#include BACKEND_SOURCE_INCLUDE

/* HOST lease model only; this never attests a VM's SMM or flash protection. */
static struct region_device root_device = REGION_DEV_INIT(NULL, 0, 0x10000);
static bool root_changed;
static bool lease_owned;
static bool fail_begin;
static bool fail_operation;
static bool fail_end;
static bool mutate_begin;
static bool mutate_operation;
static size_t begins;
static size_t ends;
static size_t accesses;
static size_t last_offset;
static size_t last_size;
static struct capsule_write_layout *mutate_layout;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		abort();
}

const struct region_device *boot_device_rw(void)
{
	return root_changed ? NULL : &root_device;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	if (mutate_layout)
		mutate_layout->route[0].flash_offset = mutate_layout->smmstore.offset;
	return !context && storage == &backend && size == sizeof(backend);
}

int qemu_pflash_lease_begin(const struct region_device *root, struct qemu_pflash_lease *lease)
{
	assert(root == &root_device && !lease_owned);
	begins++;
	if (fail_begin)
		return -1;
	lease_owned = true;
	lease->private_data[0] = 1;
	if (mutate_begin)
		backend.context.staging_size++;
	return 0;
}

static int access_media(const struct region_device *root, const struct qemu_pflash_lease *lease,
	size_t offset, size_t size)
{
	assert(root == &root_device && lease_owned && lease->private_data[0] == 1);
	accesses++;
	last_offset = offset;
	last_size = size;
	if (mutate_operation)
		backend.context.staging_size++;
	return fail_operation ? -1 : 0;
}

int qemu_pflash_lease_read(const struct region_device *root, const struct qemu_pflash_lease *lease,
	size_t offset, void *buffer, size_t size)
{
	assert(buffer);
	return access_media(root, lease, offset, size);
}

int qemu_pflash_lease_program(const struct region_device *root,
	const struct qemu_pflash_lease *lease, size_t offset, const void *buffer, size_t size)
{
	assert(buffer);
	return access_media(root, lease, offset, size);
}

int qemu_pflash_lease_erase(const struct region_device *root,
	const struct qemu_pflash_lease *lease, size_t offset, size_t size)
{
	return access_media(root, lease, offset, size);
}

int qemu_pflash_lease_sync(const struct region_device *root, const struct qemu_pflash_lease *lease)
{
	return access_media(root, lease, 0, 0);
}

int qemu_pflash_lease_end(struct qemu_pflash_lease *lease)
{
	assert(lease_owned && lease->private_data[0] == 1);
	ends++;
	lease_owned = false;
	memset(lease, 0, sizeof(*lease));
	return fail_end ? -1 : 0;
}

int main(int argc, char **argv)
{
	struct capsule_write_layout layout = {
		.revision = CAPSULE_WRITE_LAYOUT_REVISION,
		.size = sizeof(layout),
		.media_size = 0x10000,
		.erase_size = 0x1000,
		.route_count = 1,
		.metadata_count = 2,
		.metadata = {{ 0x8000, 0x2000 }, { 0xa000, 0x2000 }},
		.smmstore = { 0xe000, 0x1000 },
		.route = {{ 0x2000, 0x4000, 0x2000, LB_CAPSULE_REGION_BIOS, 0 }},
	};
	struct capsule_media_backend media;
	uint8_t staging[0x10000];
	uint8_t buffer[0x1000];
	size_t context_size = 0;
	uint64_t offset = 0x4000;
	size_t size = sizeof(buffer);

	assert(argc == 2);
	if (!strcmp(argv[1], "layout-mutation")) {
		mutate_layout = &layout;
		assert(capsule_qemu_pflash_backend_build(&layout, (uintptr_t)staging,
			sizeof(staging), protected_storage, NULL, &media, &context_size) == CB_ERR);
		assert(!begins && !ends && !accesses && !context_size);
		return 0;
	}
	assert(capsule_qemu_pflash_backend_build(&layout, (uintptr_t)staging, sizeof(staging),
		protected_storage, NULL, &media, &context_size) == CB_SUCCESS);
	assert(context_size <= CAPSULE_BROKER_CONTEXT_SIZE && media.begin && media.end);
	assert(capsule_qemu_pflash_backend_build(&layout, (uintptr_t)staging, sizeof(staging),
		protected_storage, NULL, &media, &context_size) == CB_ERR);
	assert(media.source_valid(media.context, staging + 16, sizeof(staging) - 16));
	assert(!media.source_valid(media.context, staging + 1, sizeof(staging)));

	fail_begin = !strcmp(argv[1], "begin-failure");
	mutate_begin = !strcmp(argv[1], "begin-mutation");
	if (fail_begin || mutate_begin) {
		assert(media.begin(media.context) == CB_ERR);
		assert(begins == 1 && ends == (mutate_begin ? 1U : 0U) && !lease_owned);
		assert(!accesses);
		return 0;
	}
	if (!strcmp(argv[1], "unowned")) {
		assert(media.write(media.context, offset, buffer, size) == CB_ERR);
		assert(!begins && !ends && !accesses);
		return 0;
	}
	assert(media.begin(media.context) == CB_SUCCESS);
	assert(media.begin(media.context) == CB_ERR && begins == 1);
	if (!strcmp(argv[1], "transaction")) {
		assert(media.erase(media.context, offset, size) == CB_SUCCESS);
		assert(media.write(media.context, offset, buffer, size) == CB_SUCCESS);
		assert(media.read(media.context, offset, buffer, size) == CB_SUCCESS);
		assert(last_offset == offset && last_size == size);
		assert(media.sync(media.context) == CB_SUCCESS);
		assert(media.end(media.context) == CB_SUCCESS);
		assert(begins == 1 && ends == 1 && accesses == 4 && !lease_owned);
		assert(media.begin(media.context) == CB_SUCCESS);
		assert(media.end(media.context) == CB_SUCCESS);
		return 0;
	}
	if (!strcmp(argv[1], "end-failure")) {
		fail_end = true;
		assert(media.end(media.context) == CB_ERR);
		assert(!lease_owned && ends == 1);
		assert(media.begin(media.context) == CB_ERR);
		return 0;
	}
	if (!strcmp(argv[1], "store")) {
		offset = 0xe000;
	} else if (!strcmp(argv[1], "state")) {
		offset = 0x8000;
	} else if (!strcmp(argv[1], "crossing")) {
		offset = 0x5fff;
	} else if (!strcmp(argv[1], "overflow")) {
		offset = UINT64_MAX;
	} else if (!strcmp(argv[1], "alias")) {
		assert(media.read(media.context, offset, &backend, sizeof(backend)) == CB_ERR);
		assert(!accesses);
		assert(media.end(media.context) == CB_ERR && ends == 1 && !lease_owned);
		return 0;
	} else if (!strcmp(argv[1], "root-change")) {
		root_changed = true;
	} else if (!strcmp(argv[1], "operation-failure")) {
		fail_operation = true;
	} else if (!strcmp(argv[1], "operation-mutation")) {
		mutate_operation = true;
	} else {
		abort();
	}
	assert(media.write(media.context, offset, buffer, size) == CB_ERR);
	assert(accesses == ((fail_operation || mutate_operation) ? 1U : 0U));
	assert(media.end(media.context) == CB_ERR && ends == 1 && !lease_owned);
	assert(media.begin(media.context) == CB_ERR);
	return 0;
}
