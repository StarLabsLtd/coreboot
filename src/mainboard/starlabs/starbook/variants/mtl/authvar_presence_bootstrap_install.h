/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_PRESENCE_BOOTSTRAP_INSTALL_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_PRESENCE_BOOTSTRAP_INSTALL_H

#include <boot/payload_mm_authvar_presence_bootstrap.h>
#include <cpu/x86/smm_invocation_evidence.h>

#define STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION 1U
#define STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST 1U
#define STARBOOK_MTL_PRESENCE_BOOTSTRAP_ACCEPTED 2U
#define STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST 0x425354feU
#define STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS 0x42534f4bU

struct starbook_mtl_presence_bootstrap_frame {
	uint32_t revision;
	uint32_t size;
	uint32_t state;
	uint32_t reserved;
	struct payload_mm_authvar_presence_bootstrap_receipts receipts;
	uint32_t initiator_cpu;
	uint32_t maximum_cpus;
} __aligned(8);

enum starbook_mtl_presence_bootstrap_result {
	STARBOOK_MTL_PRESENCE_NOT_BOOTSTRAP,
	STARBOOK_MTL_PRESENCE_BOOTSTRAP_IMPORTED,
	STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR,
};

#if ENV_SMM || ENV_TEST
enum starbook_mtl_presence_bootstrap_result
starbook_mtl_presence_bootstrap_receive(
	const struct smm_invocation_save_state_ops *active_ops);
enum cb_err starbook_mtl_presence_bootstrap_route_install(void);
enum cb_err starbook_mtl_presence_bootstrap_response_stage(
	const struct smm_invocation_save_state_ops *active_ops);
enum cb_err starbook_mtl_presence_bootstrap_response_publish(void);
#endif

#endif
