/* SPDX-License-Identifier: GPL-2.0-only */

#include <program_loading.h>
#include <boot/coreboot_tables.h>
#include <console/console.h>
#include <types.h>
#include <timestamp.h>

/* For each segment of a program loaded this function is called*/
void prog_segment_loaded(uintptr_t start, size_t size, int flags)
{
	platform_segment_loaded(start, size, flags);
	arch_segment_loaded(start, size, flags);
}

void __weak platform_segment_loaded(uintptr_t start,
							size_t size, int flags)
{
	/* do nothing */
}

void __weak arch_segment_loaded(uintptr_t start, size_t size,
						int flags)
{
	/* do nothing */
}

void prog_run(struct prog *prog)
{
	if (ENV_RAMSTAGE && prog_type(prog) == PROG_PAYLOAD)
		timestamp_add_now(TS_PAYLOAD_PLATFORM_FINALIZE_BEGIN);
	platform_prog_run(prog);
	if (ENV_RAMSTAGE && prog_type(prog) == PROG_PAYLOAD)
		timestamp_add_now(TS_PAYLOAD_PLATFORM_FINALIZE_END);
	if (ENV_RAMSTAGE && CONFIG(PAYLOAD_RESOURCE_HANDOFF) &&
	    prog_type(prog) == PROG_PAYLOAD) {
		timestamp_add_now(TS_PAYLOAD_RESOURCE_FINALIZE_BEGIN);
		if (lb_finalize_payload_resource_handoff() != CB_SUCCESS)
			die("Payload resource handoff finalization failed\n");
		timestamp_add_now(TS_PAYLOAD_RESOURCE_FINALIZE_END);
	}
	arch_prog_run(prog);
}

void __weak platform_prog_run(struct prog *prog)
{
	/* do nothing */
}
