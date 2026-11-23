/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef TESTS_LIB_AUTHVAR_OWNER_NOR_H
#define TESTS_LIB_AUTHVAR_OWNER_NOR_H

#include <commonlib/region.h>

enum { NOR_BLOCK = 4096, NOR_SIZE = 3 * NOR_BLOCK, LITE_NOR_SIZE = 512 * 1024 };

struct test_nor {
	struct region_device region;
	uint8_t bytes[LITE_NOR_SIZE];
	bool fail_write;
	bool partial_write;
};

extern const struct region_device_ops nor_ops;
extern bool allow_program;
bool nor_write_enabled(void);
void fixed_slot_rotation_keeps_device_revocations(void **state);

#endif
