/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef STARBOOK_MTL_AUTHVAR_SERVICE_RUNTIME_DISPATCH_H
#define STARBOOK_MTL_AUTHVAR_SERVICE_RUNTIME_DISPATCH_H

#include <cpu/x86/smm_pre_lock_dispatch.h>
#include <stdint.h>

/* All CPUs call before the legacy SMI lock. Only the fixed fc owner is handled. */
enum smm_pre_lock_dispatch_result starbook_mtl_authvar_service_runtime_dispatch(
	uint32_t cpu, uint32_t initial_apic_id);

#endif
