/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <delay.h>
#include <ec/acpi/ec.h>
#include <stdint.h>
#include "ec.h"

#define JAGUAR_EC_CMD	0x666
#define JAGUAR_EC_DATA	0x662

static void configure_ec_gpio(void)
{
	uint8_t tmp;
	uint8_t olddata = ec_read(0x31);

	/* select page c2 */
	ec_write(0x31, 0xc2);

	tmp = ec_read(0xe);
	printk(BIOS_SPEW, "EC: 0x%02x = %02x\n", 0xe, tmp);
	tmp = ec_read(0xf);
	printk(BIOS_SPEW, "EC: 0x%02x = %02x\n", 0xf, tmp);
	tmp = ec_read(0x11);
	printk(BIOS_SPEW, "PCIe GPP0 Slot-0 before Force Power for EC: 0x%02x = %02x\n", 0x11, tmp);

	/* SLOT-0 Force power */
	if (CONFIG(ENABLE_EVAL_CARD) &&
	    CONFIG(ENABLE_FORCE_POWER_GPP0) &&
	    CONFIG(PCIE_SLOT0_2X4))  {
		// Writing 0 on BIT 0 to turn on force power
		tmp |= BIT(0);
	}
	ec_write(0x11, tmp);
	printk(BIOS_SPEW, "PCIe GPP0 Slot-0 Force Power for EC: 0x%02x = %02x\n", 0x11, tmp);

	/* Configure PCIe mux */
	tmp = ec_read(0x13) & ~(BIT(2) | BIT(1) | BIT(0));

	if (CONFIG(ENABLE_NVME_PCIE_2LANES)) {
		tmp |= BIT(1);
	} else if (CONFIG(ENABLE_PCIE_4LANES)) {
		tmp |= BIT(0);
	} else if (CONFIG(ENABLE_NVME_WLAN_2LANES)) {
		tmp |= BIT(0);
		tmp |= BIT(1);
	}
	ec_write(0x13, tmp);
	printk(BIOS_SPEW, "PCIe Mux value for EC: 0x%02x = %02x\n", 0x13, tmp);

	tmp = ec_read(0x22);
	if (CONFIG(XGBE_LED_TURN_ON))
		tmp |= BIT(2);
	else
		tmp &= ~BIT(2);

	if (CONFIG(XGBE_EN))
		tmp |= BIT(0) | BIT(3);
	else
		tmp &= ~(BIT(0) | BIT(3));
	ec_write(0x22, tmp);
	printk(BIOS_SPEW, "EC: 0x%02x = %02x\n", 0x22, tmp);

	/* Enable M.2 SSD0 power */
	tmp = ec_read(0x15);
	if (CONFIG(ENABLE_NVME_4LANES) ||
	    CONFIG(ENABLE_NVME_PCIE_2LANES) ||
	    CONFIG(ENABLE_NVME_WLAN_2LANES))
		tmp |= BIT(0) | BIT(4);
	else
		tmp &= ~(BIT(0) | BIT(4));
	ec_write(0x15, tmp);
	printk(BIOS_SPEW, "EC: 0x%02x = %02x\n", 0x15, tmp);

	/* USB PD setup */
	tmp = ec_read(0x19);
	tmp |= BIT(1);
	tmp |= BIT(2);
	tmp |= BIT(3);
	ec_write(0x19, tmp);

	/* restore page */
	ec_write(0x31, olddata);

	/* Modern Standby enable, D3 cold enable */
	tmp = ec_read(0xB7);
	tmp |= BIT(7) | BIT(4) | BIT(5);
	ec_write(0xB7, tmp);
}

void jaguar_ec_init(void)
{
	ec_set_ports(JAGUAR_EC_CMD, JAGUAR_EC_DATA);
	configure_ec_gpio();
}
