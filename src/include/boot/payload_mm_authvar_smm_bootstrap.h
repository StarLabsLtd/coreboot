/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_H
#define BOOT_PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_H

#include <boot/payload_mm_authvar_mor_seal.h>
#include <boot/payload_mm_authvar_smm_loader.h>
#include <commonlib/bsd/cb_err.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_REVISION 1U

typedef bool (*payload_mm_authvar_smm_spi_restricted)(void *context);

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

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_H */
