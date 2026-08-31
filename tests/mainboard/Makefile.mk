# SPDX-License-Identifier: GPL-2.0-only

.PHONY: q35-lapic-timer-host-test
q35-lapic-timer-host-test:
	@tests/mainboard/q35_lapic_timer_test.sh
