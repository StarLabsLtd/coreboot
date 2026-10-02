/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include "lapic_timer.h"
#include "native_service.h"

void lb_board(struct lb_header *header)
{
	if (CONFIG(PAYLOAD_LOCAL_APIC_TIMER_INFO))
		q35_add_lapic_timer_table(header);
	if (CONFIG(Q35_SMM_INVOCATION_NATIVE_SERVICE_COMPONENT))
		q35_publish_native_service_table(header);
}
