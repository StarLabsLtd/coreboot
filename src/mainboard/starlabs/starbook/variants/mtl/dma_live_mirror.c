/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <commonlib/bsd/cbmem_id.h>
#include <cpu/x86/smm_invocation_loader_identity.h>

#include "dma_live_mirror.h"

void *starbook_mtl_dma_live_mirror_acquire(size_t size, uint32_t lifecycle)
{
	const struct cbmem_entry *entry;
	void *mirror;

	if (!size || (lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD &&
		lifecycle != SMM_INVOCATION_LOADER_S3_RELOAD))
		return NULL;
	if (lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD)
		mirror = cbmem_add(CBMEM_ID_MTL_DMA_MIRROR, size);
	else
		mirror = cbmem_find(CBMEM_ID_MTL_DMA_MIRROR);
	entry = cbmem_entry_find(CBMEM_ID_MTL_DMA_MIRROR);
	if (!mirror || !entry || cbmem_entry_start(entry) != mirror ||
	    cbmem_entry_size(entry) != size)
		return NULL;
	return mirror;
}
