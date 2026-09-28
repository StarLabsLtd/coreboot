/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_lifecycle_close.h>

#include "payload_mm_authvar_presence_lifecycle_close_internal.h"

static struct payload_mm_authvar_presence_lifecycle_close_slot close_slot;

struct payload_mm_authvar_presence_lifecycle_close_slot *
payload_mm_authvar_presence_warm_reset_close_slot(void)
{
	return &close_slot;
}

enum cb_err payload_mm_authvar_presence_warm_reset_close(void)
{
	return payload_mm_authvar_presence_lifecycle_close_warm_reset(&close_slot);
}

#if ENV_TEST
void payload_mm_authvar_presence_warm_reset_close_reset_test(void)
{
	close_slot = (struct payload_mm_authvar_presence_lifecycle_close_slot) { 0 };
}
#endif
