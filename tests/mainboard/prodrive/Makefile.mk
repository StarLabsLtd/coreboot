# SPDX-License-Identifier: GPL-2.0-only

tests-y += atlas-options-test
atlas-options-test-srcs += tests/mainboard/prodrive/atlas-options-test.c
atlas-options-test-config += CONFIG_OPTION_BACKEND_NONE=0 CONFIG_ONBOARD_VGA_IS_PRIMARY=0
atlas-options-test-stage := romstage
