/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_AUTHVAR_INTERNAL_H
#define PAYLOAD_MM_AUTHVAR_INTERNAL_H

#include <boot/payload_mm_authvar.h>

bool payload_mm_authvar_range_end(uint64_t base, uint64_t size, uint64_t *end);
bool payload_mm_authvar_range_within(uint64_t base, uint64_t size,
	uint64_t outer_base, uint64_t outer_size);
bool payload_mm_authvar_contract_valid(
	const struct payload_mm_authvar_contract *contract);

#endif
