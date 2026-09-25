/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_SMM_SPI_WINDOW_H
#define SOC_INTEL_COMMON_BLOCK_SMM_SPI_WINDOW_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

enum intel_smm_spi_window_owner {
	INTEL_SMM_SPI_WINDOW_LEGACY_SMMSTORE = 0x534d4d53,
	INTEL_SMM_SPI_WINDOW_AUTHVAR = 0x41555448,
	INTEL_SMM_SPI_WINDOW_OPTION_STORE = 0x4f50544e,
};

/*
 * An opaque, single-transaction ownership token. It must be zeroed before
 * begin and must not be copied or modified until end consumes it.
 */
struct intel_smm_spi_window {
	uintptr_t private_data[4];
};

typedef int (*intel_smm_spi_window_operation)(void *context);

int intel_smm_spi_window_begin(struct intel_smm_spi_window *window,
	enum intel_smm_spi_window_owner owner, const void *context);
int intel_smm_spi_window_prove(const struct intel_smm_spi_window *window,
	enum intel_smm_spi_window_owner owner, const void *context);
int intel_smm_spi_window_end(struct intel_smm_spi_window *window,
	enum intel_smm_spi_window_owner owner, const void *context);
int intel_smm_spi_window_run(enum intel_smm_spi_window_owner owner,
	const void *owner_context, intel_smm_spi_window_operation operation,
	void *operation_context, int failure_result);

/* Prove that the effective policy is live and the idle controller is RO. */
bool intel_smm_spi_writes_restricted(void);

/* Restore the controller to its protected idle state outside a transaction. */
int intel_smm_spi_window_restore_ro(void);

#endif /* SOC_INTEL_COMMON_BLOCK_SMM_SPI_WINDOW_H */
