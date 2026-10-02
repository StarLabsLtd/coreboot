/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_QEMU_Q35_PUBLIC_SERVICE_H
#define MAINBOARD_QEMU_Q35_PUBLIC_SERVICE_H

#include <stdint.h>
#include <types.h>

enum q35_public_service_result {
	Q35_PUBLIC_SERVICE_ERROR = -1,
	Q35_PUBLIC_SERVICE_REFUSED,
	Q35_PUBLIC_SERVICE_HELD,
};

/* Public command ownership only; never private invocation/presence evidence. */
enum q35_public_service_result q35_public_service_begin(uint32_t cpu,
	uint32_t initial_apic_id);
bool q35_public_service_current(void);
void q35_public_service_end(void);

#endif
