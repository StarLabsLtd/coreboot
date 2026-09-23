/* SPDX-License-Identifier: GPL-2.0-only */

#define main payload_mm_authvar_acceptance_embedded_main
#include "payload_mm_authvar_executor_acceptance_test.c"
#undef main

static void geometry_table(void)
{
	struct payload_mm_authvar_ftw_geometry geometry;

	for (size_t blocks = 0; blocks < 3U; blocks++)
		assert(payload_mm_authvar_ftw_geometry(&geometry,
			blocks * BLOCK_SIZE, BLOCK_SIZE) == CB_ERR);
	for (size_t blocks = 3U; blocks <= 8U; blocks++) {
		size_t spare_blocks = blocks / 2U;
		size_t variable_blocks = blocks - spare_blocks - 1U;

		assert(payload_mm_authvar_ftw_geometry(&geometry,
			blocks * BLOCK_SIZE, BLOCK_SIZE) == CB_SUCCESS);
		assert(geometry.variable_offset == 0U &&
			geometry.variable_size == variable_blocks * BLOCK_SIZE &&
			geometry.working_offset == geometry.variable_size &&
			geometry.working_size == BLOCK_SIZE &&
			geometry.spare_offset == geometry.working_offset + BLOCK_SIZE &&
			geometry.spare_size == spare_blocks * BLOCK_SIZE);
	}
	assert(payload_mm_authvar_ftw_geometry(&geometry, REGION_SIZE - 1U,
		BLOCK_SIZE) == CB_ERR);
	assert(payload_mm_authvar_ftw_geometry(&geometry, REGION_SIZE, 0) == CB_ERR);
	assert(payload_mm_authvar_ftw_geometry(&geometry, (size_t)UINT32_MAX + 1U,
		BLOCK_SIZE) == CB_ERR);
}

int main(void)
{
	struct shared_state *shared = mmap(NULL, sizeof(*shared),
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	uint32_t direct_sizes[32];
	uint32_t reclaim_sizes[64];
	uint32_t erase_count;
	uint32_t fault_counts[FAULT_END + 1U] = { 0 };

	assert(shared != MAP_FAILED);
	geometry_table();
	assert(run_clean_and_direct(shared, direct_sizes,
		ARRAY_SIZE(direct_sizes)) > 0U);
	assert(discover_reclaim_programs(shared, reclaim_sizes,
		ARRAY_SIZE(reclaim_sizes), &erase_count, fault_counts) > 0U);
	assert(erase_count > 0U && independent_ftw_clean(shared->media));
	assert(independent_logical_value(shared->media) == LOGICAL_SECOND);
	for (uint32_t i = 0; i < shared->trace_count; i++) {
		const struct trace_entry *entry = &shared->trace[i];

		if (entry->kind != TRACE_READ && entry->kind != TRACE_PROGRAM &&
		    entry->kind != TRACE_ERASE)
			continue;
		assert(entry->offset <= REGION_SIZE &&
			entry->size <= REGION_SIZE - entry->offset);
		if (entry->kind == TRACE_ERASE)
			assert(entry->size == ERASE_SIZE &&
				!(entry->offset % ERASE_SIZE));
	}
	run_workspace_checkpoint(shared);
	if (SPARE_SIZE > VARIABLE_SIZE) {
		run_spare_suffix_overrun(shared);
		run_workspace_suffix_overrun(shared);
	}
	assert(munmap(shared, sizeof(*shared)) == 0);
	return 0;
}
