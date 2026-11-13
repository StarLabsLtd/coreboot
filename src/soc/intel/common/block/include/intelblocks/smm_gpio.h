/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_SMM_GPIO_H
#define SOC_INTEL_COMMON_BLOCK_SMM_GPIO_H

#include <gpio.h>
#include <intelblocks/smm_gpio_defs.h>
#include <stdbool.h>

bool mainboard_smm_gpio_allowed(gpio_t pad);
bool mainboard_acpi_gpio_uses_smm(gpio_t pad);
void smm_gpio_handler(void);

#endif
