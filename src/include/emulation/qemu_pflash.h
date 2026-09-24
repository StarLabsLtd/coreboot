/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EMULATION_QEMU_PFLASH_H
#define EMULATION_QEMU_PFLASH_H

#include <stddef.h>
#include <stdint.h>

struct region_device;

/*
 * Opaque owner handle for one QEMU pflash transaction. The object must be
 * zeroed before begin and must not be copied or changed while it is owned.
 */
struct qemu_pflash_lease {
	uintptr_t private_data[4];
};

int qemu_pflash_lease_begin(const struct region_device *root,
	struct qemu_pflash_lease *lease);
int qemu_pflash_lease_read(const struct region_device *root,
	const struct qemu_pflash_lease *lease, size_t offset, void *buffer,
	size_t size);
int qemu_pflash_lease_program(const struct region_device *root,
	const struct qemu_pflash_lease *lease, size_t offset,
	const void *buffer, size_t size);
int qemu_pflash_lease_erase(const struct region_device *root,
	const struct qemu_pflash_lease *lease, size_t offset, size_t size);
int qemu_pflash_lease_sync(const struct region_device *root,
	const struct qemu_pflash_lease *lease);
int qemu_pflash_lease_end(struct qemu_pflash_lease *lease);

#endif
