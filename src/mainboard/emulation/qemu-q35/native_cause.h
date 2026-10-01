/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_QEMU_Q35_NATIVE_CAUSE_H
#define MAINBOARD_QEMU_Q35_NATIVE_CAUSE_H

#include <cpu/x86/smm_invocation_evidence.h>

/* Controlled component only; no arbitrary-OS invocation admission. */
enum cb_err q35_native_cause_begin(uint8_t command,
	const struct smm_invocation_save_state_ops **ops);
bool q35_native_cause_current(void);
void q35_native_cause_end(void);

#endif
