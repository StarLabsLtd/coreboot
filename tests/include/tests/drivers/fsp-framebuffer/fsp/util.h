/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TEST_FSP_UTIL_H
#define TEST_FSP_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Firmware HOB lookup is the hardware-dependency boundary for this fixture. */
const void *fsp_find_extension_hob_by_guid(const uint8_t *guid, size_t *size);

#endif
