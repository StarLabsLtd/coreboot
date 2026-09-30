/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/x86/smm.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define TEST_TSEG_BASE 0x100000U
#define TEST_TSEG_SIZE 0x100000U

void smm_region(uintptr_t *base, size_t *size)
{
	*base = TEST_TSEG_BASE;
	*size = TEST_TSEG_SIZE;
}

int printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
	return 0;
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		abort();
}

int main(void)
{
	uintptr_t handler_base;
	uintptr_t cache_base;
	uintptr_t chipset_base;
	size_t handler_size;
	size_t cache_size;
	size_t chipset_size;

	assert(!smm_subregion(SMM_SUBREGION_HANDLER, &handler_base, &handler_size));
	assert(!smm_subregion(SMM_SUBREGION_CACHE, &cache_base, &cache_size));
	assert(!smm_subregion(SMM_SUBREGION_CHIPSET, &chipset_base, &chipset_size));
	assert(handler_base == TEST_TSEG_BASE);
	assert(cache_size == CONFIG_SMM_RESERVED_SIZE);
	assert(chipset_size == CONFIG_IED_REGION_SIZE);
	assert(cache_base + cache_size == chipset_base);
	assert(chipset_base + chipset_size == TEST_TSEG_BASE + TEST_TSEG_SIZE);

#if CONFIG(SMM_OPAL_S3_STATE_SMRAM)
	uintptr_t opal_base;
	size_t opal_size;

	assert(!smm_subregion(SMM_SUBREGION_OPAL_S3_STATE, &opal_base, &opal_size));
	assert(opal_size == CONFIG_SMM_OPAL_S3_STATE_SMRAM_SIZE);
	assert(opal_base + opal_size == cache_base);
#else
	assert(smm_subregion(SMM_SUBREGION_OPAL_S3_STATE,
		&cache_base, &cache_size));
#endif

#if CONFIG(SMM_AUTHVAR_S3_STATE_SMRAM)
	uintptr_t authvar_base;
	size_t authvar_size;

	assert(!smm_subregion(SMM_SUBREGION_AUTHVAR_S3_STATE,
		&authvar_base, &authvar_size));
	assert(authvar_size == CONFIG_SMM_AUTHVAR_S3_STATE_SMRAM_SIZE);
	assert(handler_base + handler_size == authvar_base);
#if CONFIG(SMM_OPAL_S3_STATE_SMRAM)
	assert(authvar_base + authvar_size == opal_base);
#else
	assert(authvar_base + authvar_size == cache_base);
#endif
#else
#if CONFIG(SMM_OPAL_S3_STATE_SMRAM)
	assert(handler_base + handler_size == opal_base);
#else
	assert(handler_base + handler_size == cache_base);
#endif
#endif

	return 0;
}
