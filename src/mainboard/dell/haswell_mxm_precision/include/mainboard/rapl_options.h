/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_RAPL_OPTIONS_H
#define MAINBOARD_RAPL_OPTIONS_H

/* Stored Dell value for disabling the short-duration package limit. */
#define RAPL_PL2_UNLIMITED_W	4096

#define RAPL_PL1_MIN	37
#define RAPL_PL1_MAX	57
#define RAPL_PL1_STEP	1

#define RAPL_PL2_MIN	37
#define RAPL_PL2_MAX	RAPL_PL2_UNLIMITED_W
#define RAPL_PL2_STEP	1

#endif /* MAINBOARD_RAPL_OPTIONS_H */
