/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpigen.h>
#include <intelblocks/smm_gpio.h>

bool __weak mainboard_acpi_gpio_uses_smm(gpio_t pad)
{
	return false;
}

static int acpigen_soc_gpio_op(const char *op, unsigned int gpio_num)
{
	/* op (gpio_num) */
	acpigen_emit_namestring(op);
	acpigen_write_integer(gpio_num);
	return 0;
}

static int acpigen_soc_gpio_op_status(const char *op, unsigned int gpio_num, uint8_t status_op)
{
	int ret;

	acpigen_write_store();
	ret = acpigen_soc_gpio_op(op, gpio_num);
	acpigen_emit_byte(status_op);
	return ret;
}

static int acpigen_soc_get_gpio_state(const char *op, unsigned int gpio_num)
{
	/* Store (op (gpio_num), Local0) */
	acpigen_write_store();
	acpigen_soc_gpio_op(op, gpio_num);
	acpigen_emit_byte(LOCAL0_OP);
	return 0;
}

int acpigen_soc_read_rx_gpio(unsigned int gpio_num)
{
	return acpigen_soc_get_gpio_state("\\_SB.PCI0.GRXS", gpio_num);
}

int acpigen_soc_get_tx_gpio(unsigned int gpio_num)
{
	if (CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_GPIO) && mainboard_acpi_gpio_uses_smm(gpio_num))
		return acpigen_soc_get_gpio_state("\\_SB.PCI0.PGTX", gpio_num);

	return acpigen_soc_get_gpio_state("\\_SB.PCI0.GTXS", gpio_num);
}

int acpigen_soc_set_tx_gpio(unsigned int gpio_num)
{
	if (CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_GPIO) && mainboard_acpi_gpio_uses_smm(gpio_num))
		return acpigen_soc_gpio_op("\\_SB.PCI0.PSTX", gpio_num);

	return acpigen_soc_gpio_op("\\_SB.PCI0.STXS", gpio_num);
}

int acpigen_soc_clear_tx_gpio(unsigned int gpio_num)
{
	if (CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_GPIO) && mainboard_acpi_gpio_uses_smm(gpio_num))
		return acpigen_soc_gpio_op("\\_SB.PCI0.PCTX", gpio_num);

	return acpigen_soc_gpio_op("\\_SB.PCI0.CTXS", gpio_num);
}

int acpigen_soc_set_tx_gpio_status(unsigned int gpio_num, uint8_t status_op)
{
	int ret;

	if (CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_GPIO) && mainboard_acpi_gpio_uses_smm(gpio_num))
		return acpigen_soc_gpio_op_status("\\_SB.PCI0.PSTX", gpio_num, status_op);

	ret = acpigen_soc_set_tx_gpio(gpio_num);
	acpigen_write_store_int_to_op(ret ? 1 : 0, status_op);
	return ret;
}

int acpigen_soc_clear_tx_gpio_status(unsigned int gpio_num, uint8_t status_op)
{
	int ret;

	if (CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_GPIO) && mainboard_acpi_gpio_uses_smm(gpio_num))
		return acpigen_soc_gpio_op_status("\\_SB.PCI0.PCTX", gpio_num, status_op);

	ret = acpigen_soc_clear_tx_gpio(gpio_num);
	acpigen_write_store_int_to_op(ret ? 1 : 0, status_op);
	return ret;
}
