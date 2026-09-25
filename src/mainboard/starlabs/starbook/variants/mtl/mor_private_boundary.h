/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_MOR_PRIVATE_BOUNDARY_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_MOR_PRIVATE_BOUNDARY_H

#include <stdbool.h>

#if ENV_TEST
void starbook_mtl_mor_private_boundary_reset_test(void);
bool starbook_mtl_mor_private_boundary_secrets_zero_test(void);
#endif

#endif /* MAINBOARD_STARLABS_STARBOOK_MTL_MOR_PRIVATE_BOUNDARY_H */
