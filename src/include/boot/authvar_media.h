/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_AUTHVAR_MEDIA_H
#define BOOT_AUTHVAR_MEDIA_H

#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_writer.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

struct authvar_media {
	uint8_t *image;
	uint8_t *candidate;
	uint8_t *verify;
	uint8_t *workspace;
	uint8_t *store;
	size_t image_size;
	size_t candidate_size;
	size_t store_size;
	uint32_t block_size;
	struct payload_mm_authvar_ftw_plan plan;
	bool failed;
};

/* Owner supplies serialized I/O and buffers; write and erase require readback. */
struct authvar_media_io {
	void *context;
	uint64_t (*read)(void *context, uint32_t offset, void *buffer, size_t size);
	uint64_t (*write)(void *context, uint32_t offset, const void *buffer, size_t size);
	uint64_t (*erase)(void *context, uint32_t offset, size_t size);
	void (*recovery)(void *context, const struct payload_mm_authvar_ftw_plan *plan,
		unsigned int pass, uint64_t status);
};

/* I/O callbacks are borrowed for the call, never retained across address changes. */
uint64_t authvar_media_initialize(struct authvar_media *media,
	const struct authvar_media_io *io);
uint64_t authvar_media_commit(struct authvar_media *media,
	const struct authvar_media_io *io, const void *store, size_t size,
	const struct payload_mm_authvar_write_plan *append, const void *record);

#endif
