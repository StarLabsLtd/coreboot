/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STARLABS_PHX_POWER_BUDGET_H
#define STARLABS_PHX_POWER_BUDGET_H

#define PHX_POWER_BUDGET_APMC 0xe8
#define PHX_POWER_BUDGET_READY 0xa5
#define PHX_POWER_BUDGET_ERROR 0x5a

#ifndef __ACPI__
#include <stdbool.h>
#include <stdint.h>
bool phx_power_budget(uint8_t available_watts);
#endif
#endif
