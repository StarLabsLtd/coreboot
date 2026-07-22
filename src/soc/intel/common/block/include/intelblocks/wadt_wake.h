/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_WADT_WAKE_H
#define SOC_INTEL_COMMON_BLOCK_WADT_WAKE_H

#include <stdbool.h>
#include <stdint.h>

/* True when WADT should be preserved/restored for this sleep type. */
bool wadt_wake_should_preserve(uint8_t slp_typ);

/* Read whether the OS currently has WADT enabled. */
bool wadt_wake_is_enabled(void);

/* GPE0 STD status bits to keep across the pre-sleep status clear. */
uint32_t wadt_wake_status_preserve_mask(bool enabled);

/* Restore the sampled WADT enable state after board sleep finalize. */
void wadt_wake_restore(uint8_t slp_typ, bool enabled);

#endif /* SOC_INTEL_COMMON_BLOCK_WADT_WAKE_H */
