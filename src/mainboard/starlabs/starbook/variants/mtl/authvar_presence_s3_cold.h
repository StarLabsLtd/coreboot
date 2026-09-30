/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_COLD_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_COLD_H

#include <boot/payload_mm_authvar_presence_lifecycle_close_route.h>
#include <commonlib/bsd/cb_err.h>
#include <cpu/x86/smm_invocation_loader_instance.h>

enum cb_err starbook_mtl_authvar_presence_s3_cold_install(
	const struct smm_invocation_loader_instance *instance);
enum cb_err starbook_mtl_authvar_presence_s3_cold_route_complete(
	struct payload_mm_authvar_presence_lifecycle_close_route *route);
enum cb_err starbook_mtl_authvar_presence_s3_suspend(void);
bool starbook_mtl_authvar_presence_s3_protected_storage(
	void *unused, const void *object, size_t size);
bool starbook_mtl_authvar_presence_s3_record_storage(
	void **storage, size_t *size);
bool starbook_mtl_authvar_presence_s3_cold_active(void);

#if ENV_TEST
void starbook_mtl_authvar_presence_s3_cold_reset_test(void);
uint32_t starbook_mtl_authvar_presence_s3_cold_state_test(void);
#endif

#endif
