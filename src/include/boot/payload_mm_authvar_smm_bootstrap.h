/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_H
#define BOOT_PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_H

#include <boot/payload_mm_authvar_mor_seal.h>
#include <boot/payload_mm_authvar_smm_loader.h>
#include <commonlib/bsd/cb_err.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_SMM_MEDIA_REVISION 1U

typedef bool (*payload_mm_authvar_smm_spi_restricted)(void *context);

struct payload_mm_authvar_smm_media_facts {
	uint32_t revision;
	uint32_t size;
	uint64_t boot_media_size;
	uint64_t store_offset;
	uint64_t store_size;
	uint32_t block_size;
	uint32_t erase_size;
	uint32_t reserved[2];
};

struct payload_mm_authvar_smm_media_ops {
	uint32_t revision;
	uint32_t size;
	enum cb_err (*facts)(void *context,
		struct payload_mm_authvar_smm_media_facts *facts);
	enum cb_err (*install)(void *context);
	void *context;
	size_t context_size;
	uint32_t reserved[2];
};

_Static_assert(sizeof(struct payload_mm_authvar_smm_media_facts) == 48,
	"SMM media facts ABI changed");

/*
 * Private loader-to-SMM input. The loader reserves arena in SMRAM and a
 * fixed transport outside SMRAM; the platform callback re-reads its chipset
 * write-control state. No member is a payload or OS ABI.
 */
struct payload_mm_authvar_smm_bootstrap {
	uint32_t revision;
	uint32_t size;
	uint64_t cold_boot_generation;
	struct payload_mm_authvar_mor_seal_channel seal_channel;
	payload_mm_authvar_smm_spi_restricted spi_writes_restricted_to_smm;
	void *spi_context;
	size_t spi_context_size;
	uint32_t reserved[2];
};

/* Calculate the exact arena and limits for a runtime SMMSTORE size. */
enum cb_err payload_mm_authvar_smm_bootstrap_arena_size(uint64_t store_size,
	size_t *arena_size);

/* One terminal, private SMM initialization attempt; publishes no endpoint. */
enum cb_err payload_mm_authvar_smm_bootstrap_install(
	const struct payload_mm_authvar_smm_bootstrap *bootstrap);

/* Private SMM platform binding; returns callbacks resident in protected SMM. */
bool platform_payload_mm_authvar_smm_media_ops(
	struct payload_mm_authvar_smm_media_ops *ops);

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_H */
