/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef TEST_ROM_ARMOR_MSR_H
#define TEST_ROM_ARMOR_MSR_H

#include <cpu/x86/msr_access.h>

msr_t rom_armor_test_rdmsr(unsigned int index);
#define rdmsr rom_armor_test_rdmsr

#endif
