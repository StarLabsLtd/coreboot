/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <intelblocks/gpio.h>
#include <intelblocks/nvs.h>
#include <intelblocks/smm_gpio.h>

bool __weak mainboard_smm_gpio_allowed(gpio_t pad)
{
	return false;
}

void smm_gpio_handler(void)
{
	gpio_t pad;
	uint8_t op;
	int value;
	int ret = -1;

	if (!gnvs)
		return;

	pad = gnvs->gpio_pad;
	op = gnvs->gpio_op;

	gnvs->gpio_status = 1;
	if (!mainboard_smm_gpio_allowed(pad))
		return;

	switch (op) {
	case SMM_GPIO_GET:
		ret = gpio_sbi_get_tx(pad, &value);
		if (!ret)
			gnvs->gpio_value = value;
		break;
	case SMM_GPIO_SET:
		ret = gpio_sbi_set_tx(pad, 1);
		break;
	case SMM_GPIO_CLEAR:
		ret = gpio_sbi_set_tx(pad, 0);
		break;
	}

	if (!ret)
		gnvs->gpio_status = 0;
}
