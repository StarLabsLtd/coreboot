/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_fixed_policy.h>

#include "native_smm.h"

extern const uint8_t ass_pk[], ass_pk_end[], ass_kek[], ass_kek_end[], ass_db[], ass_db_end[];
extern const uint8_t ass_dbx_baseline[], ass_dbx_baseline_end[];
extern const uint8_t ass_dbx_append[], ass_dbx_append_end[];
extern const uint8_t ass_dbx_update[], ass_dbx_update_end[];

const struct authvar_fixed_policy *ass_native_fixed_policy(void)
{
	static struct authvar_fixed_policy policy;
	struct payload_mm_authvar_auth2_view baseline;

	policy = (struct authvar_fixed_policy) {
		.factory_pk = { ass_pk, ass_pk_end - ass_pk },
		.factory_kek = { ass_kek, ass_kek_end - ass_kek },
		.factory_db = { ass_db, ass_db_end - ass_db },
		.dbx_baseline = { ass_dbx_baseline, ass_dbx_baseline_end - ass_dbx_baseline },
		.dbx_append = { ass_dbx_append, ass_dbx_append_end - ass_dbx_append },
		.dbx_update = { ass_dbx_update, ass_dbx_update_end - ass_dbx_update },
	};
	if (!authvar_fixed_keys_valid(&policy) ||
	    payload_mm_authvar_auth2_parse(policy.dbx_baseline.data, policy.dbx_baseline.size,
		&baseline) != PAYLOAD_MM_AUTHVAR_FORMAT_OK ||
	    !payload_mm_authvar_auth2_metadata_valid(&baseline) ||
	    !baseline.payload.size || !policy.dbx_append.size || !policy.dbx_update.size)
		return NULL;
	return &policy;
}
