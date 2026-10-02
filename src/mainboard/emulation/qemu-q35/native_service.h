/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_Q35_NATIVE_SERVICE_H
#define MAINBOARD_Q35_NATIVE_SERVICE_H

#include <boot/payload_mm_authvar_presence_bootstrap.h>
#include <commonlib/coreboot_tables.h>

struct lb_header;
void q35_publish_native_service_table(struct lb_header *header);
/* Actual held exclusive E8 owner; metadata remains available after CLOSE. */
bool q35_capsule_service_current(void);
/* The same current owner plus the installed, irreversible RAM eligibility. */
bool q35_capsule_ram_transaction_current(void);
/* Existing private storage and sole-write checks, reused by the cold provider. */
bool q35_capsule_storage_is_protected(void *context, const void *base, size_t size);
bool q35_capsule_writes_are_private(void *context);
enum cb_err q35_capsule_broker_boot_install(void);

#define Q35_NATIVE_BOOTSTRAP_REVISION 2U
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
	/* Zero in requests and in compositions without the installed capsule service. */
	struct lb_capsule_broker_endpoint capsule_endpoint;
} __aligned(8);

_Static_assert(sizeof(struct q35_native_bootstrap_frame) <= 4096,
	"private bootstrap completion must fit its owned page");

#endif
