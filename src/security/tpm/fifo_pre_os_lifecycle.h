/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TPM_FIFO_PRE_OS_LIFECYCLE_H
#define SECURITY_TPM_FIFO_PRE_OS_LIFECYCLE_H

#include <commonlib/bsd/cb_err.h>
#include <security/tpm/pre_os_lifecycle.h>

/* One-shot transfer of the initialized TPM2 FIFO route into lifecycle. */
enum cb_err tpm2_fifo_pre_os_lifecycle_install(
	struct tpm_pre_os_lifecycle *lifecycle);

/* The legacy route remains revoked regardless of the handoff result. */
enum cb_err tpm2_fifo_pre_os_lifecycle_handoff(
	struct tpm_pre_os_lifecycle *lifecycle);

#endif /* SECURITY_TPM_FIFO_PRE_OS_LIFECYCLE_H */
