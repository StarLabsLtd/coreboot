# SPDX-License-Identifier: GPL-2.0-only

tests-y += mtrr-test

mtrr-test-srcs += tests/cpu/mtrr-test.c
mtrr-test-srcs += src/device/device_util.c
mtrr-test-srcs += tests/stubs/console.c
mtrr-test-config += CONFIG_RESERVE_MTRRS_FOR_OS=1
