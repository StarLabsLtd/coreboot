/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <boot/dma_handoff.h>
#include <bootstate.h>
#include <commonlib/bsd/cb_err.h>
#include <console/console.h>

#include "dma_live_platform.h"

static void starbook_mtl_dma_enable(void *unused)
{
	(void)unused;
	if (starbook_mtl_dma_live_backend_ensure() != CB_SUCCESS)
		die("StarBook MTL DMA: protected transaction failed");
}

BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_EXIT, starbook_mtl_dma_enable, NULL);

bool payload_dma_handoff_blob(uintptr_t *address, size_t *bytes)
{
	return starbook_mtl_dma_live_backend_handoff(address, bytes);
}
