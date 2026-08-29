/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_EMULATION_QEMU_Q35_LAPIC_TIMER_H
#define MAINBOARD_EMULATION_QEMU_Q35_LAPIC_TIMER_H

#include <stdint.h>

struct q35_lapic_timer_ops {
	uint32_t (*lapic_read)(uint32_t reg);
	void (*lapic_write)(uint32_t reg, uint32_t value);
	uint32_t (*pm_timer_read)(void);
};

uint32_t q35_measure_lapic_timer_hz(const struct q35_lapic_timer_ops *ops,
	uint32_t poll_limit);

#endif
