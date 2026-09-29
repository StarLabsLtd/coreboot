/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <boot/dma_handoff.h>
#include <bootstate.h>
#include <commonlib/bsd/cb_err.h>
#include <console/console.h>

#include "dma_live_platform.h"
#if CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
#include "dma_smm_receipt_provision.h"
#endif

static void starbook_mtl_dma_enable(void *unused)
{
	(void)unused;
	if (starbook_mtl_dma_live_backend_ensure() != CB_SUCCESS)
		die("StarBook MTL DMA: protected transaction failed");
#if CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
	if (starbook_mtl_dma_receipt_provision_send() != CB_SUCCESS)
		die("StarBook MTL DMA: protected receipt provisioning failed");
#endif
}

BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_EXIT, starbook_mtl_dma_enable, NULL);

bool payload_dma_handoff_blob(uintptr_t *address, size_t *bytes)
{
	return starbook_mtl_dma_live_backend_handoff(address, bytes);
}
