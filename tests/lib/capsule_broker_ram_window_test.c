/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>

extern int dprintf(int descriptor, const char *format, ...);

#undef assert
#define assert(condition) do { \
	if (!(condition)) { \
		dprintf(2, "RAM_WINDOW_ASSERT: %s\n", #condition); \
		__builtin_abort(); \
	} \
} while (0)

/* Reuse the actual broker fixture and modeled media, not a second owner. */
#define main capsule_broker_original_main
#include "capsule_broker_test.c"
#undef main

int main(int argc, char **argv)
{
	assert(argc == 2);
	ram_window_case(argv[1]);
	return 0;
}
