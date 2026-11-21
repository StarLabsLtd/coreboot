/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_fixed_policy.h>
#include <commonlib/helpers.h>
#include <string.h>

#include "native_smm.h"
#include "wire.h"
#include <ass/fixed_policy.h>

extern const uint8_t ass_dbx_baseline[], ass_dbx_baseline_end[];
extern const uint8_t ass_dbx_append[], ass_dbx_append_end[];
extern const uint8_t ass_dbx_update[], ass_dbx_update_end[];

const struct authvar_fixed_policy *ass_native_fixed_policy(void)
{
	static struct authvar_fixed_certificate pk[1], kek[3], db[5];
	static struct authvar_fixed_policy policy;
	struct payload_mm_authvar_auth2_view baseline;

	policy = (struct authvar_fixed_policy) {
		.pk = pk, .kek = kek, .db = db,
		.factory_pk = { fixed_pk, sizeof(fixed_pk) },
		.factory_kek = { fixed_kek, sizeof(fixed_kek) },
		.factory_db = { fixed_db, sizeof(fixed_db) },
		.pk_count = authvar_fixed_decode_certificates(
			(struct payload_mm_authvar_span) { fixed_pk, sizeof(fixed_pk) },
			pk, ARRAY_SIZE(pk)),
		.kek_count = authvar_fixed_decode_certificates(
			(struct payload_mm_authvar_span) { fixed_kek, sizeof(fixed_kek) },
			kek, ARRAY_SIZE(kek)),
		.db_count = authvar_fixed_decode_certificates(
			(struct payload_mm_authvar_span) { fixed_db, sizeof(fixed_db) },
			db, ARRAY_SIZE(db)),
		.dbx_baseline = { ass_dbx_baseline, ass_dbx_baseline_end - ass_dbx_baseline },
		.dbx_append = { ass_dbx_append, ass_dbx_append_end - ass_dbx_append },
		.dbx_update = { ass_dbx_update, ass_dbx_update_end - ass_dbx_update },
	};
	if (policy.pk_count != ARRAY_SIZE(pk) || policy.kek_count != ARRAY_SIZE(kek) ||
	    policy.db_count != ARRAY_SIZE(db) ||
	    payload_mm_authvar_auth2_parse(policy.dbx_baseline.data, policy.dbx_baseline.size,
		&baseline) != PAYLOAD_MM_AUTHVAR_FORMAT_OK ||
	    !payload_mm_authvar_auth2_metadata_valid(&baseline) ||
	    !baseline.payload.size || !policy.dbx_append.size || !policy.dbx_update.size ||
	    baseline.payload.size > sizeof(fixed_dbx) ||
	    policy.dbx_append.size != sizeof(fixed_dbx) - baseline.payload.size ||
	    memcmp(fixed_dbx, baseline.payload.data, baseline.payload.size) ||
	    memcmp(fixed_dbx + baseline.payload.size, policy.dbx_append.data, policy.dbx_append.size))
		return NULL;
	return &policy;
}
