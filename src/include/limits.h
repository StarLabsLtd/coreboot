/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIMITS_H
#define LIMITS_H

#define CHAR_BIT	8

#define USHRT_MAX	(SHRT_MAX * 2U + 1U)
#define SHRT_MAX	__SHRT_MAX__
#define SHRT_MIN	(-SHRT_MAX - 1)
#define INT_MAX		__INT_MAX__
#define INT_MIN		(-INT_MAX - 1)
#define UINT_MAX	(INT_MAX * 2U + 1U)
#define LONG_MAX	__LONG_MAX__
#define LONG_MIN	(-LONG_MAX - 1)
#define ULONG_MAX	(LONG_MAX * 2UL + 1UL)
#define LLONG_MAX	__LONG_LONG_MAX__
#define LLONG_MIN	(-LLONG_MAX - 1)
#define ULLONG_MAX	(LLONG_MAX * 2ULL + 1ULL)
#define UINTPTR_MAX	ULONG_MAX

#endif /* LIMITS_H */
