/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */
#ifndef SMM_AUTHVAR_DISPATCH_H
#define SMM_AUTHVAR_DISPATCH_H

#include <boot/authvar_owner.h>
#include <boot/authvar_policy.h>
#include <boot/payload_mm_authvar_service.h>

/* Private SMM state; the adapter serializes initialization and every dispatch. */
struct smm_authvar_dispatch {
	struct authvar_owner owner;
	struct authvar_policy policy;
	const struct authvar_fixed_policy *fixed;
};

/* Both fixed-size messages are distinct, aligned private SMRAM allocations. */
enum cb_err smm_authvar_dispatch(struct smm_authvar_dispatch *service,
	const struct lb_authvar_service_endpoint *endpoint,
	const void *request, void *response, size_t size);
uint64_t smm_authvar_dispatch_option(struct smm_authvar_dispatch *service,
	const uint16_t *name, uint32_t value);

#endif
