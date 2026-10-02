/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker_buffers.h>

#define main runtime_view_regression_main
#include "owned-runtime-fixture.h"
#undef main

extern int dprintf(int fd, const char *format, ...);
#undef assert
#define assert(condition) do { \
	if (!(condition)) { \
		dprintf(2, "OWNED_DMA_ASSERT: %s\n", #condition); \
		__builtin_abort(); \
	} \
} while (0)

#define smm_runtime test_memory.runtime
#include "owned-memory-fragment.h"
#undef smm_runtime

int main(void)
{
	struct smm_runtime unchanged;
	const struct smm_dma_owned_memory **output = &test_memory.dma_output;

	assert(runtime_view_regression_main() == 0);
	reset_runtime();
	hook_mode = 0;
	test_memory.runtime.dma_owned_memory = (struct smm_dma_owned_memory) {
		.table_base = 0x10000000U,
		.table_size = 40960U,
		.arena_base = 0x10100000U,
		.arena_size = 161U * 4096U,
	};
	test_memory.runtime.capsule_communication_base = 0x11000000U;
	test_memory.runtime.capsule_communication_reserved_size = 4096U;
	test_memory.runtime.capsule_communication_size = 4096U;
	test_memory.runtime.capsule_staging_base = 0x11100000U;
	test_memory.runtime.capsule_staging_size = 9437184U;
	assert(smm_get_dma_owned_memory(output));
	assert(*output == &test_memory.runtime.dma_owned_memory);
	unchanged = test_memory.runtime;
	assert(!smm_get_dma_owned_memory(NULL));
	assert(!smm_get_dma_owned_memory((void *)((uintptr_t)output + 1U)));
	assert(!smm_get_dma_owned_memory((void *)&test_memory.runtime));
	assert(!smm_get_dma_owned_memory((void *)&test_memory.runtime.dma_owned_memory));
	assert(!memcmp(&unchanged, &test_memory.runtime, sizeof(unchanged)));
	assert(!smm_get_dma_owned_memory((void *)&test_memory.save_state[0][0]));
	for (size_t byte = 0; byte < sizeof(test_memory.save_state); byte++)
		assert(((uint8_t *)test_memory.save_state)[byte] == 0);
	test_memory.runtime.dma_owned_memory.table_size = 40961U;
	*output = NULL;
	assert(!smm_get_dma_owned_memory(output));
	assert(!*output);
	test_memory.runtime.dma_owned_memory = unchanged.dma_owned_memory;
	test_memory.runtime.dma_owned_memory.table_base =
		test_memory.runtime.capsule_staging_base;
	assert(!smm_get_dma_owned_memory(output));
	assert(!*output);
	return 0;
}
