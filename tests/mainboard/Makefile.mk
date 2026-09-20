# SPDX-License-Identifier: GPL-2.0-only

tests-y += q35-lapic-timer-test

q35-lapic-timer-test-srcs += tests/mainboard/q35-lapic-timer-test.c
q35-lapic-timer-test-srcs += src/mainboard/emulation/qemu-q35/lapic_timer.c
q35-lapic-timer-test-cflags += -I src/mainboard/emulation/qemu-q35
q35-lapic-timer-test-no_test_framework := 1

.PHONY: q35-lapic-timer-host-test
q35-lapic-timer-host-test:
	@tests/mainboard/q35_lapic_timer_test.sh
