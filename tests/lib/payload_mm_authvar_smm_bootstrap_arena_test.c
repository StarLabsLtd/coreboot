/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_smm_bootstrap.h>
#include <inttypes.h>
#include <stdint.h>

int printf(const char *format, ...);

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

int main(void)
{
	size_t small = 1;
	size_t normal = 1;
	size_t repeated = 1;

	CHECK(payload_mm_authvar_smm_bootstrap_arena_size(3U * 65536U, &small) ==
		CB_SUCCESS);
	CHECK(payload_mm_authvar_smm_bootstrap_arena_size(8U * 65536U, &normal) ==
		CB_SUCCESS);
	CHECK(payload_mm_authvar_smm_bootstrap_arena_size(8U * 65536U, &repeated) ==
		CB_SUCCESS);
	CHECK(small > 3U * 3U * 65536U && normal > 3U * 8U * 65536U);
	CHECK(normal > small && normal == repeated);
	CHECK(payload_mm_authvar_smm_bootstrap_arena_size(3U * 65536U - 1U,
		&repeated) == CB_ERR && repeated == 0);
	CHECK(payload_mm_authvar_smm_bootstrap_arena_size(UINT64_MAX, &repeated) ==
		CB_ERR && repeated == 0);
	CHECK(payload_mm_authvar_smm_bootstrap_arena_size(8U * 65536U, NULL) ==
		CB_ERR);
	printf("arena-192k=%zu arena-512k=%zu\n", small, normal);
	return 0;
}
