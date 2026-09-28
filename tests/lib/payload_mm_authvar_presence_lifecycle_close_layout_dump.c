/* SPDX-License-Identifier: GPL-2.0-only */
#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
int printf(const char *format, ...);
int main(void)
{
	printf("endpoint\t%zu\tgeneration=%zu\tbase=%zu\tsource_mask=%zu\n",
		sizeof(struct lb_authvar_presence_lifecycle_close_endpoint),
		offsetof(struct lb_authvar_presence_lifecycle_close_endpoint, generation),
		offsetof(struct lb_authvar_presence_lifecycle_close_endpoint, communication_base),
		offsetof(struct lb_authvar_presence_lifecycle_close_endpoint, source_mask));
	printf("message\t%zu\talign=%zu\tstatus=%zu\tcompletion=%zu\n",
		sizeof(struct payload_mm_authvar_presence_lifecycle_close_message),
		_Alignof(struct payload_mm_authvar_presence_lifecycle_close_message),
		offsetof(struct payload_mm_authvar_presence_lifecycle_close_message, status),
		offsetof(struct payload_mm_authvar_presence_lifecycle_close_message, completion));
	return 0;
}
