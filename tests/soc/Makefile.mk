# SPDX-License-Identifier: GPL-2.0-only

tests-y += amd-smu-test

amd-smu-test-srcs += tests/soc/amd-smu-test.c
amd-smu-test-config += CONFIG_HAVE_MONOTONIC_TIMER=1
amd-smu-test-cflags += -I src/soc/amd/common/block/include
amd-smu-test-cflags += -I src/soc/amd/phoenix/include
