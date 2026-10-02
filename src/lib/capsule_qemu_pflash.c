/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <commonlib/region.h>
#include <emulation/qemu_pflash.h>
#include <string.h>

#include "capsule_qemu_pflash_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "The capsule QEMU pflash transport is SMM-only"
#endif

enum media_state {
	MEDIA_IDLE,
	MEDIA_BEGINNING,
	MEDIA_ACTIVE,
	MEDIA_ENDING,
};

struct media_context {
	const struct region_device *root;
	uintptr_t staging_base;
	size_t staging_size;
};

_Static_assert(sizeof(struct media_context) <= CAPSULE_BROKER_CONTEXT_SIZE,
	"capsule QEMU media context exceeds the private broker limit");

static struct {
	struct media_context context;
	struct media_context sealed;
	struct capsule_write_layout layout;
	struct qemu_pflash_lease lease;
	uint32_t install_attempted;
	uint32_t state;
	uint32_t poisoned;
} backend;

static bool buffers_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t a = (uintptr_t)left;
	uintptr_t b = (uintptr_t)right;

	return a > UINTPTR_MAX - left_size || b > UINTPTR_MAX - right_size ||
		(a < b + right_size && b < a + left_size);
}

static void poison(void)
{
	__atomic_store_n(&backend.poisoned, 1, __ATOMIC_RELEASE);
}

static bool context_current(const void *context)
{
	if (!context || !backend.sealed.root ||
	    __atomic_load_n(&backend.poisoned, __ATOMIC_ACQUIRE) ||
	    memcmp(&backend.context, &backend.sealed, sizeof(backend.sealed)) ||
	    memcmp(context, &backend.sealed, sizeof(backend.sealed)) ||
	    boot_device_rw() != backend.sealed.root ||
	    region_device_offset(backend.sealed.root) ||
	    region_device_sz(backend.sealed.root) != backend.layout.media_size) {
		poison();
		return false;
	}
	return true;
}

static bool span_allowed(uint64_t offset, size_t size)
{
	if (!size || offset > SIZE_MAX || offset > backend.layout.media_size ||
	    size > backend.layout.media_size - offset)
		return false;
	for (size_t i = 0; i < backend.layout.route_count; i++) {
		const struct lb_capsule_update_region *route = &backend.layout.route[i];

		if (offset >= route->flash_offset && size <= route->size &&
		    offset - route->flash_offset <= route->size - size)
			return true;
	}
	return false;
}

static bool operation_allowed(const void *context, uint64_t offset, size_t size)
{
	return context_current(context) &&
		__atomic_load_n(&backend.state, __ATOMIC_ACQUIRE) == MEDIA_ACTIVE &&
		span_allowed(offset, size);
}

static enum cb_err media_begin(void *context)
{
	uint32_t expected = MEDIA_IDLE;

	if (!context_current(context) ||
	    !__atomic_compare_exchange_n(&backend.state, &expected, MEDIA_BEGINNING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
		return CB_ERR;
	if (qemu_pflash_lease_begin(backend.sealed.root, &backend.lease)) {
		poison();
		__atomic_store_n(&backend.state, MEDIA_IDLE, __ATOMIC_RELEASE);
		return CB_ERR;
	}
	if (!context_current(context)) {
		(void)qemu_pflash_lease_end(&backend.lease);
		__atomic_store_n(&backend.state, MEDIA_IDLE, __ATOMIC_RELEASE);
		return CB_ERR;
	}
	__atomic_store_n(&backend.state, MEDIA_ACTIVE, __ATOMIC_RELEASE);
	return CB_SUCCESS;
}

static enum cb_err media_end(void *context)
{
	uint32_t expected = MEDIA_ACTIVE;
	bool valid = context_current(context);
	int result;

	if (!__atomic_compare_exchange_n(&backend.state, &expected, MEDIA_ENDING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
		poison();
		return CB_ERR;
	}
	result = qemu_pflash_lease_end(&backend.lease);
	if (result || !valid || !context_current(context))
		poison();
	__atomic_store_n(&backend.state, MEDIA_IDLE, __ATOMIC_RELEASE);
	return __atomic_load_n(&backend.poisoned, __ATOMIC_ACQUIRE) ? CB_ERR : CB_SUCCESS;
}

static enum cb_err media_read(void *context, uint64_t offset, void *buffer, size_t size)
{
	if (!buffer || buffers_overlap(buffer, size, &backend, sizeof(backend)) ||
	    !operation_allowed(context, offset, size) ||
	    qemu_pflash_lease_read(backend.sealed.root, &backend.lease,
		(size_t)offset, buffer, size) || !context_current(context)) {
		poison();
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static enum cb_err media_erase(void *context, uint64_t offset, size_t size)
{
	if (!operation_allowed(context, offset, size) ||
	    size != backend.layout.erase_size || offset % backend.layout.erase_size ||
	    qemu_pflash_lease_erase(backend.sealed.root, &backend.lease,
		(size_t)offset, size) || !context_current(context)) {
		poison();
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static enum cb_err media_write(void *context, uint64_t offset, const void *buffer, size_t size)
{
	if (!buffer || buffers_overlap(buffer, size, &backend, sizeof(backend)) ||
	    !operation_allowed(context, offset, size) ||
	    qemu_pflash_lease_program(backend.sealed.root, &backend.lease,
		(size_t)offset, buffer, size) || !context_current(context)) {
		poison();
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static enum cb_err media_sync(void *context)
{
	/* Attempt the owned fence even when an earlier callback poisoned policy. */
	bool valid = context_current(context);

	if (__atomic_load_n(&backend.state, __ATOMIC_ACQUIRE) != MEDIA_ACTIVE)
		return CB_ERR;
	if (qemu_pflash_lease_sync(backend.sealed.root, &backend.lease) ||
	    !valid || !context_current(context)) {
		poison();
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static bool media_source_valid(void *context, const void *source, size_t size)
{
	uintptr_t address = (uintptr_t)source;

	return context_current(context) && source && size &&
		address >= backend.sealed.staging_base &&
		size <= backend.sealed.staging_size &&
		address - backend.sealed.staging_base <= backend.sealed.staging_size - size;
}

enum cb_err capsule_qemu_pflash_backend_build(const struct capsule_write_layout *layout,
	uintptr_t staging_base, size_t staging_size,
	capsule_broker_protected_storage_fn storage_is_protected, void *storage_context,
	struct capsule_media_backend *media, size_t *context_size)
{
	uint32_t expected = 0;
	const struct region_device *root;
	struct capsule_write_layout snapshot;

	if (!layout || !media || !context_size || !storage_is_protected)
		return CB_ERR;
	snapshot = *layout;
	if (!capsule_write_layout_valid(&snapshot) || snapshot.media_size > SIZE_MAX ||
	    snapshot.erase_size != 4096U || !staging_base || !staging_size ||
	    staging_base > UINTPTR_MAX - staging_size ||
	    buffers_overlap((void *)staging_base, staging_size, &backend, sizeof(backend)) ||
	    buffers_overlap(media, sizeof(*media), &backend, sizeof(backend)) ||
	    buffers_overlap(context_size, sizeof(*context_size), &backend, sizeof(backend)) ||
	    buffers_overlap(media, sizeof(*media), context_size, sizeof(*context_size)) ||
	    buffers_overlap(media, sizeof(*media), layout, sizeof(*layout)) ||
	    buffers_overlap(context_size, sizeof(*context_size), layout, sizeof(*layout)) ||
	    !storage_is_protected(storage_context, &backend, sizeof(backend)) ||
	    memcmp(layout, &snapshot, sizeof(snapshot)) ||
	    !__atomic_compare_exchange_n(&backend.install_attempted, &expected, 1,
		false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
		return CB_ERR;
	root = boot_device_rw();
	if (!root || region_device_offset(root) || region_device_sz(root) != snapshot.media_size)
		return CB_ERR;
	backend.context = (struct media_context) { root, staging_base, staging_size };
	backend.sealed = backend.context;
	backend.layout = snapshot;
	*media = (struct capsule_media_backend) {
		.context = &backend.context,
		.size = snapshot.media_size,
		.erase_size = snapshot.erase_size,
		.read = media_read,
		.erase = media_erase,
		.write = media_write,
		.sync = media_sync,
		.source_valid = media_source_valid,
		.begin = media_begin,
		.end = media_end,
	};
	*context_size = sizeof(backend.context);
	return CB_SUCCESS;
}
