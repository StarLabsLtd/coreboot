/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_AUTHVAR_IMAGE_POLICY_H
#define BOOT_AUTHVAR_IMAGE_POLICY_H

#define AUTHVAR_IMAGE_POLICY_SNAPSHOT_REVISION 1U
#define AUTHVAR_IMAGE_POLICY_SNAPSHOT_HEADER_SIZE 96U
/* Maximum admitted variable size in this wire format. */
#define AUTHVAR_IMAGE_POLICY_SNAPSHOT_MAX_VARIABLE_SIZE (1024U * 1024U)

enum authvar_image_policy_snapshot_key {
	/* PK/global namespace; db/dbx/dbt/image-security namespace, fixed order. */
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_PK,
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_DB,
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_DBX,
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_DBT,
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_KEYS,
};

#endif
