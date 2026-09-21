/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DRIVERS_PC80_TPM_TPM_H
#define DRIVERS_PC80_TPM_TPM_H

#include <security/tpm/tis.h>

tis_sendrecv_fn pc80_tis_probe(enum tpm_family *family);

#if CONFIG(TPM2_FIFO_PRE_OS_LIFECYCLE) && ENV_RAMSTAGE
#include <commonlib/bsd/cb_err.h>
#include <stdbool.h>
/* These are for the single late-ramstage owner of an initialized FIFO TPM. */
bool pc80_tis_is_fifo_route(tis_sendrecv_fn sendrecv);
enum cb_err pc80_tis_fifo_quiesce(void);
enum cb_err pc80_tis_fifo_validate_idle(void);
enum cb_err pc80_tis_fifo_release_locality(void);
#if ENV_TEST
struct pc80_tis_fifo_test_backend {
	u8 (*read_access)(int locality);
	u8 (*read_status)(int locality);
	void (*write_access)(u8 value, int locality);
	tpm_result_t (*command_ready)(u8 locality);
	tpm_result_t (*wait_access)(int locality, u8 mask, u8 expected);
};
void pc80_tis_fifo_set_test_backend(
	const struct pc80_tis_fifo_test_backend *backend);
#endif
#endif

#endif /* DRIVERS_PC80_TPM_TPM_H */
