/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cbmem.h>
#include <commonlib/bsd/cbmem_id.h>
#include <cpu/x86/smm_invocation_loader_identity.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dma_live_mirror.h"

struct cbmem_entry {
	uint32_t unused;
};

static struct cbmem_entry entry;
static uint8_t retained[4096];
static void *find_result;
static void *entry_start;
static size_t entry_size;
static unsigned int add_calls;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

void *cbmem_add(uint32_t id, uint64_t size)
{
	assert(id == CBMEM_ID_MTL_DMA_MIRROR);
	add_calls++;
	return size == sizeof(retained) ? retained : NULL;
}

void *cbmem_find(uint32_t id)
{
	assert(id == CBMEM_ID_MTL_DMA_MIRROR);
	return find_result;
}

const struct cbmem_entry *cbmem_entry_find(uint32_t id)
{
	assert(id == CBMEM_ID_MTL_DMA_MIRROR);
	return entry_start ? &entry : NULL;
}

void *cbmem_entry_start(const struct cbmem_entry *candidate)
{
	assert(candidate == &entry);
	return entry_start;
}

uint64_t cbmem_entry_size(const struct cbmem_entry *candidate)
{
	assert(candidate == &entry);
	return entry_size;
}

static void reset_fixture(void)
{
	find_result = retained;
	entry_start = retained;
	entry_size = sizeof(retained);
	add_calls = 0;
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	reset_fixture();
	if (!strcmp(argv[1], "cold")) {
		assert(starbook_mtl_dma_live_mirror_acquire(sizeof(retained),
			SMM_INVOCATION_LOADER_NON_S3_LOAD) == retained);
		assert(add_calls == 1);
	} else if (!strcmp(argv[1], "s3")) {
		assert(starbook_mtl_dma_live_mirror_acquire(sizeof(retained),
			SMM_INVOCATION_LOADER_S3_RELOAD) == retained);
		assert(add_calls == 0);
	} else if (!strcmp(argv[1], "missing")) {
		find_result = NULL;
		entry_start = NULL;
		assert(!starbook_mtl_dma_live_mirror_acquire(sizeof(retained),
			SMM_INVOCATION_LOADER_S3_RELOAD));
		assert(add_calls == 0);
	} else if (!strcmp(argv[1], "wrong-size")) {
		entry_size--;
		assert(!starbook_mtl_dma_live_mirror_acquire(sizeof(retained),
			SMM_INVOCATION_LOADER_S3_RELOAD));
		assert(add_calls == 0);
	} else if (!strcmp(argv[1], "moved")) {
		entry_start++;
		assert(!starbook_mtl_dma_live_mirror_acquire(sizeof(retained),
			SMM_INVOCATION_LOADER_S3_RELOAD));
		assert(add_calls == 0);
	} else if (!strcmp(argv[1], "zero-size")) {
		assert(!starbook_mtl_dma_live_mirror_acquire(0,
			SMM_INVOCATION_LOADER_NON_S3_LOAD));
		assert(add_calls == 0);
	} else if (!strcmp(argv[1], "invalid-lifecycle")) {
		assert(!starbook_mtl_dma_live_mirror_acquire(sizeof(retained), 0));
		assert(add_calls == 0);
	} else {
		abort();
	}
	return 0;
}
