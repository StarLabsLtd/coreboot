/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_Q35_NATIVE_SERVICE_H
#define MAINBOARD_Q35_NATIVE_SERVICE_H

#include <boot/payload_mm_authvar_presence_bootstrap.h>
#include <commonlib/coreboot_tables.h>

#define Q35_NATIVE_BOOTSTRAP_REVISION 1U
#define Q35_NATIVE_BOOTSTRAP_REQUEST 1U
#define Q35_NATIVE_BOOTSTRAP_COMPLETE 2U
#define Q35_NATIVE_BOOTSTRAP_WIRE_REQUEST 0x425354fcU
#define Q35_NATIVE_BOOTSTRAP_WIRE_SUCCESS 0x42534f4bU

/* Private boot transport only; no physical-presence request or grant. */
struct q35_native_bootstrap_frame {
	uint32_t revision;
	uint32_t size;
	uint32_t state;
	uint32_t reserved;
	struct payload_mm_authvar_presence_bootstrap_receipts receipts;
	struct lb_authvar_service_endpoint endpoint;
	uint64_t probe_offset;
	uint64_t probe_size;
	uint32_t write_denied;
	uint32_t maximum_cpus;
} __aligned(8);

#endif
