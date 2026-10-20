/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_SMM_GPIO_DEFS_H
#define SOC_INTEL_COMMON_BLOCK_SMM_GPIO_DEFS_H

#define SMM_GPIO_APM_PORT	0xb2
#define APM_CNT_GPIO		0xe8

#define SMM_GPIO_GET	0
#define SMM_GPIO_SET	1
#define SMM_GPIO_CLEAR	2

/* PGTX returns GPIO values 0/1, so errors must remain outside that range. */
#define SMM_GPIO_ERROR	2

#endif
