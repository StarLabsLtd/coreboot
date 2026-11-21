/* SPDX-License-Identifier: GPL-2.0-only */
/* Private prototype ABI: all pointers crossing the mode bridge are IA32. */
#ifndef PHX_GOP_CONTEXT_H
#define PHX_GOP_CONTEXT_H

#include <stdint.h>
struct gop_range32 {
	uint32_t base, size;
};
struct gop_context {
	uint32_t magic, version, length, reserved;
	struct gop_range32 helper, stack, pool, image, trace;
	uint32_t trace_used, trace_dropped;
	uint64_t uma_base, uma_size;
};
#define GOP_CONTEXT_MAGIC   0x50484750U
#define GOP_CONTEXT_VERSION 1U
_Static_assert(sizeof(struct gop_context) == 80, "Cross-mode ABI size");
_Static_assert(__builtin_offsetof(struct gop_context, uma_base) == 64, "Cross-mode ABI offset");

/* Does not dereference caller-supplied memory or modify the context. Caller
 * must first prove that the context object itself is accessible and owned. */
static inline int gop_context_valid(const struct gop_context *c)
{
	if (!c || c->magic != GOP_CONTEXT_MAGIC || c->version != GOP_CONTEXT_VERSION ||
	    c->length != sizeof(*c) || c->reserved || c->trace_used || c->trace_dropped)
		return 0;
	if (!c->uma_size || (c->uma_base & 0xffffffU) || (c->uma_size & 0xfffffU) ||
	    c->uma_base >= (1ULL << 48) || c->uma_size > (1ULL << 48) - c->uma_base)
		return 0;
	/* UMA is a GPU physical range, not an IA32 CPU pointer. */
	if ((c->uma_size >> 20) > 0xffffffffULL)
		return 0;
	const struct gop_range32 ranges[] = {c->helper, c->stack, c->pool, c->image, c->trace};
	for (unsigned int i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
		uint64_t start = ranges[i].base, end = start + ranges[i].size;
		/* Leave alignment headroom: allocators may round a cursor up to4KiB. */
		if (start < 0x10000 || !ranges[i].size || end > 0xfffff000ULL ||
		    (start & (i == 1 ? 15 : 4095)) || (ranges[i].size & (i == 1 ? 15 : 4095)))
			return 0;
		if (start < c->uma_base + c->uma_size && c->uma_base < end)
			return 0;
		for (unsigned int j = 0; j < i; j++)
			if (start < (uint64_t)ranges[j].base + ranges[j].size &&
			    ranges[j].base < end)
				return 0;
	}
	return c->pool.size >= 0x10000 && c->image.size >= 0x21000 && c->trace.size >= 4096 &&
	       c->trace.size <= 0x100000;
}
#endif
