/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_LOADER_INSTANCE_AUTHORITY_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_LOADER_INSTANCE_AUTHORITY_H

#include <commonlib/bsd/cb_err.h>
#include <stdint.h>

enum cb_err mainboard_loader_instance_authority_lifecycle(uint32_t *lifecycle);

#endif
