/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */
#ifndef SMM_CAPSULE_H
#define SMM_CAPSULE_H

#include <commonlib/capsule_service.h>
#include <commonlib/region.h>
#include <boot/authvar_owner.h>

#define SMM_CAPSULE_ERASE_SIZE 4096U
#define SMM_CAPSULE_MAX_REGIONS 8U

struct smm_authvar_dispatch;
struct lb_header;

/* Serialized with the variable owner; none of these are payload entry points. */
bool smm_capsule_initialize(void);
uint32_t smm_capsule_exec(struct smm_authvar_dispatch *service);
void smm_capsule_close(void);
bool smm_capsule_state_name(const uint16_t *name, const void *guid);
void smm_capsule_endpoint(struct lb_capsule_broker_endpoint *endpoint,
	uintptr_t communication, uintptr_t staging);
bool smm_capsule_regions(const void *fmap, size_t size,
	struct lb_capsule_update_region *regions, uint32_t *count);
void lb_smm_capsule(struct lb_header *header);

#endif
