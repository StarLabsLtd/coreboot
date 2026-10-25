/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <smm_call.h>
#include <smmstore.h>
#include <types.h>
#include <cbmem.h>

void lb_smmstorev2(struct lb_header *header)
{
	struct lb_record *rec;
	struct lb_smmstorev2 *store;
	uintptr_t communication;
	size_t communication_size;

	if (smmstore_get_com_buffer(&communication, &communication_size) < 0)
		return;

	struct smmstore_params_info info;
	smmstore_get_info(&info);

	rec = lb_new_record(header);
	store = (struct lb_smmstorev2 *)rec;

	store->tag = LB_TAG_SMMSTOREV2;
	store->size = sizeof(*store);
	store->com_buffer = communication;
	store->com_buffer_size = communication_size;
	if (info.mmap_addr < 4ULL * GiB)
		store->mmap_addr_deprecated = info.mmap_addr;
	else
		store->mmap_addr_deprecated = 0;
	store->mmap_addr = info.mmap_addr;
	store->num_blocks = info.num_blocks;
	store->block_size = info.block_size;
	store->apm_cmd = APM_CNT_SMMSTORE;
}
