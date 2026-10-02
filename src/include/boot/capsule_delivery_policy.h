/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_CAPSULE_DELIVERY_POLICY_H
#define BOOT_CAPSULE_DELIVERY_POLICY_H

#include <stdbool.h>

struct lb_header;

/* Default false. A platform must establish its real warm-reset DRAM persistence. */
bool platform_capsule_ram_persistent(void);
void lb_add_capsule_delivery_policy(struct lb_header *header);

#endif
