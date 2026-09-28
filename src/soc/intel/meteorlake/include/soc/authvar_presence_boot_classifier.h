/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_METEORLAKE_AUTHVAR_PRESENCE_BOOT_CLASSIFIER_H
#define SOC_INTEL_METEORLAKE_AUTHVAR_PRESENCE_BOOT_CLASSIFIER_H

#include <commonlib/bsd/cb_err.h>
#include <stdint.h>

struct chipset_power_state;

#define MTL_AUTHVAR_PRESENCE_BOOT_EVIDENCE_REVISION 1U

enum mtl_authvar_presence_boot_class {
	MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN,
	MTL_AUTHVAR_PRESENCE_BOOT_RESET,
	MTL_AUTHVAR_PRESENCE_BOOT_S3,
	MTL_AUTHVAR_PRESENCE_BOOT_S4,
	MTL_AUTHVAR_PRESENCE_BOOT_COLD,
};

/*
 * Versioned diagnostic evidence only. It neither grants presence authority
 * nor replaces the loader-instance lifecycle.
 */
struct mtl_authvar_presence_boot_evidence {
	uint32_t revision;
	uint32_t size;
	uint32_t classification;
	uint16_t pm1_sts;
	uint16_t reserved;
	uint32_t pm1_cnt;
	uint32_t gen_pmcon_a;
	uint32_t gblrst_cause[2];
	uint32_t hpr_cause0;
	uint32_t normalized_sleep_state;
};

_Static_assert(sizeof(struct mtl_authvar_presence_boot_evidence) == 40,
	"MTL presence boot evidence layout changed");

/* power_state must be 32-bit aligned, exclusively owned and immutable. */
enum cb_err mtl_authvar_presence_boot_classify(
	const struct chipset_power_state *power_state,
	struct mtl_authvar_presence_boot_evidence *evidence);

#endif
