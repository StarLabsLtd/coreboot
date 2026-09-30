/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_H

#include <boot/payload_mm_authvar_presence_lifecycle_close_s3_route.h>
#include <boot/payload_mm_authvar_presence_s3_record.h>
#include <commonlib/bsd/cb_err.h>

#define STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST 0x5233524d415200feULL
#define STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_CLOSING 0x5233524d434c53feULL
#define STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_SUCCESS 0x5233524d4f4b00feULL

struct starbook_mtl_authvar_presence_s3_binding {
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route;
	const struct smm_invocation_save_state_ops *retained_ops;
};

enum cb_err starbook_mtl_authvar_presence_s3_rearm_borrow(
	const struct smm_invocation_loader_instance *instance,
	struct payload_mm_authvar_presence_s3_facts *facts);
enum cb_err starbook_mtl_authvar_presence_s3_rearm_complete(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_save_state_ops *ops,
	const struct payload_mm_authvar_presence_s3_facts *facts);
enum cb_err starbook_mtl_authvar_presence_s3_binding_get(
	struct starbook_mtl_authvar_presence_s3_binding *binding);
enum cb_err starbook_mtl_authvar_presence_s3_rearm_suspend(void);
void starbook_mtl_authvar_presence_s3_rearm_poison(void);

enum cb_err starbook_mtl_authvar_presence_s3_rearm_send(void);

#if ENV_TEST
void starbook_mtl_authvar_presence_s3_rearm_reset_test(void);
#endif

#endif
