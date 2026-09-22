/* SPDX-License-Identifier: GPL-2.0-only */

#include <soc/iomap.h>
#include <soc/vtd.h>

uintptr_t soc_vtd_iop_base(void)
{
	return VTVC0_BASE_ADDRESS;
}
