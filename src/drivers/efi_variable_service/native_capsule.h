/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EFI_VARIABLE_SERVICE_NATIVE_CAPSULE_H
#define EFI_VARIABLE_SERVICE_NATIVE_CAPSULE_H

#include <types.h>
#include "capsule_store.h"

struct authvar_owner;
struct ass_capsule_source;
struct payload_mm_crypto_owner;
struct payload_mm_crypto_span;

uint64_t ass_native_capsule_floor(struct authvar_owner *owner,
	const uint8_t image_guid[16], uint32_t compiled_floor, uint32_t *floor);

struct ass_capsule_geometry {
	uint8_t image_guid[16];
	uint32_t flash_size;
	uint32_t block_size;
	uint32_t compiled_floor;
};

/* The geometry is supplied by coreboot, never by the caller of the capsule service. */
uint32_t ass_native_capsule_info(struct authvar_owner *owner,
	const struct ass_capsule_geometry *geometry, void *message, size_t size);
uint64_t ass_native_capsule_target(struct ass_capsule_source *source,
	const struct ass_capsule_geometry *geometry, uint32_t image_offset, uint32_t floor);
/* Return the selected names as an index mask into the trusted allowlist. */
uint64_t ass_native_capsule_manifest(struct ass_capsule_source *source,
	const struct ass_capsule_geometry *geometry, const char (*allowlist)[16],
	size_t allowed, uint32_t *selected);
struct ass_capsule_range {
	uint32_t offset;
	uint32_t size;
};

#define ASS_CAPSULE_MAX_RANGES 32

/* Shared ASS capsule communication GUID, encoded as the IA32 wire bytes. */
#define ASS_CAPSULE_GUID_BYTES \
	{ 0xc8, 0x76, 0x1d, 0x3e, 0xfd, 0xea, 0x19, 0x46, \
	  0xb5, 0x0b, 0xe7, 0x7b, 0xd1, 0x83, 0x9d, 0x9d }

enum ass_capsule_command {
	ASS_CAPSULE_INFO = 1,
	ASS_CAPSULE_BEGIN = 2,
	ASS_CAPSULE_PROGRAM = 3,
	ASS_CAPSULE_CLOSE = 5,
};

struct ass_capsule_wire_request {
	uint32_t revision;
	uint32_t command;
	uint32_t transaction;
};

/* Validate the common request and return the payload after its revision header. */
bool ass_capsule_wire_payload(void *message, size_t size,
	struct ass_capsule_wire_request *request, uint8_t **payload, size_t *payload_size);

/* Publish a selection only after all selected ranges and the store match. */
uint64_t ass_native_capsule_select_ranges(struct ass_capsule_source *source,
	const struct ass_capsule_geometry *geometry, struct ass_capsule_range fmap,
	struct ass_capsule_range store, const char (*names)[16],
	const struct ass_capsule_range *ranges, size_t count, uint32_t *selected);

/* The platform callback owns write protection and immutable block handling. */
typedef uint32_t ass_capsule_program_fn(uint32_t, const struct efi_capsule_range *,
	uint32_t, uint8_t *, uint32_t);

struct ass_capsule_plan {
	struct efi_capsule_range ranges[ASS_CAPSULE_MAX_RANGES];
	uint32_t count, blocks, next;
	uint32_t trial_slot;
};

struct ass_capsule_transaction {
	struct ass_capsule_plan plan;
	uint32_t active;
	uint32_t last;
	uint32_t floor;
};

uint64_t ass_native_capsule_begin(struct ass_capsule_plan *plan,
	struct ass_capsule_source *source,
	const struct ass_capsule_geometry *geometry, struct ass_capsule_range fmap,
	struct ass_capsule_range store, struct ass_capsule_range bios, const char (*names)[16],
	const struct ass_capsule_range *ranges, size_t count, uint32_t floor,
	uint8_t *image, ass_capsule_program_fn *program, uint32_t boot_slot);
uint64_t ass_native_capsule_program(struct ass_capsule_plan *plan,
	struct ass_capsule_source *source, uint32_t block, uint8_t *image,
	ass_capsule_program_fn *program);
uint32_t ass_native_capsule_activate(struct ass_capsule_transaction *transaction);
void ass_native_capsule_close(struct ass_capsule_transaction *transaction,
	struct ass_capsule_source *source);
uint32_t ass_native_capsule_continue(struct ass_capsule_transaction *transaction,
	struct ass_capsule_source *source, void *message, size_t size, uint8_t *image,
	ass_capsule_program_fn *program, uint32_t (*commit)(uint32_t slot));

struct ass_capsule_platform {
	struct ass_capsule_geometry geometry;
	struct ass_capsule_range fmap, store, bios;
	const char (*names)[16];
	const struct ass_capsule_range *ranges;
	size_t count;
	uint32_t boot_slot; /* 0: non-A/B; 1/2: executing RW slot; 3: RO recovery. */
	const struct payload_mm_crypto_span *root;
	/* Validate the entire request range as usable, non-protected RAM. */
	const void *(*map_source)(uint64_t address, size_t size);
	ass_capsule_program_fn *program;
	uint32_t (*commit)(uint32_t slot);
};

/* All bounds are exclusive at the upper end; the mailbox is never image RAM. */
bool ass_capsule_source_in_ram(uint64_t address, size_t size, uintptr_t ram_start,
	uintptr_t ram_end, uintptr_t mailbox, size_t mailbox_size);

uint32_t ass_native_capsule_dispatch(struct ass_capsule_transaction *transaction,
	struct ass_capsule_source *source, struct authvar_owner *owner,
	struct payload_mm_crypto_owner *crypto, const struct ass_capsule_platform *platform,
	void *message, size_t size, uint8_t *image);

#endif
