/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_fixed_policy.h>
#include <commonlib/helpers.h>
#include <string.h>

#include "native_smm.h"
#include "wire.h"
#include ASS_FIXED_POLICY_HEADER

const struct authvar_fixed_policy *ass_native_fixed_policy(void)
{
	static struct authvar_fixed_certificate pk[1], kek[3], db[5];
	static struct authvar_fixed_policy policy;

	policy = (struct authvar_fixed_policy) {
		.pk = pk, .kek = kek, .db = db,
		.factory_pk = { fixed_pk, sizeof(fixed_pk) },
		.factory_kek = { fixed_kek, sizeof(fixed_kek) },
		.factory_db = { fixed_db, sizeof(fixed_db) },
		.factory_dbx = { fixed_dbx, sizeof(fixed_dbx) },
		.pk_count = authvar_fixed_decode_certificates(
			(struct payload_mm_authvar_span) { fixed_pk, sizeof(fixed_pk) },
			pk, ARRAY_SIZE(pk)),
		.kek_count = authvar_fixed_decode_certificates(
			(struct payload_mm_authvar_span) { fixed_kek, sizeof(fixed_kek) },
			kek, ARRAY_SIZE(kek)),
		.db_count = authvar_fixed_decode_certificates(
			(struct payload_mm_authvar_span) { fixed_db, sizeof(fixed_db) },
			db, ARRAY_SIZE(db)),
	};
	if (policy.pk_count != ARRAY_SIZE(pk) || policy.kek_count != ARRAY_SIZE(kek) ||
	    policy.db_count != ARRAY_SIZE(db))
		return NULL;
	return &policy;
}
