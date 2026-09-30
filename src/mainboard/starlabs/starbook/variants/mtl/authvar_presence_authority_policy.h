/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef STARBOOK_MTL_AUTHVAR_PRESENCE_AUTHORITY_POLICY_H
#define STARBOOK_MTL_AUTHVAR_PRESENCE_AUTHORITY_POLICY_H

#include <boot/payload_mm_authvar_presence_route_session.h>

enum cb_err starbook_mtl_authvar_presence_authority_policy_get(
	const struct payload_mm_authvar_presence_route_authority_policy **policy);

#endif
