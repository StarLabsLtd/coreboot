/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AMD_PEI_GOP_LOADER_H
#define AMD_PEI_GOP_LOADER_H
#include <stddef.h>
#include <stdint.h>
uintptr_t amd_pei_gop_load_rmodule(const void *blob, size_t length, void *destination,
				   size_t capacity);
#endif
