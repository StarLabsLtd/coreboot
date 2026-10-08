/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <amdblocks/acpi.h>
#include <amdblocks/amd_pci_util.h>
#include <amdblocks/acpimmio.h>
#include <amdblocks/pci_clk_req.h>
#include <acpi/acpi.h>
#include <acpi/acpigen.h>
#include <commonlib/helpers.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pciexp.h>
#include <drivers/usb/pci_xhci/pci_xhci.h>
#include <static.h>
#include <variants.h>
#include <ec/starlabs/merlin/ec.h>

/* The IRQ mapping in fch_irq_map ends up getting written to the indirect address space that is
   accessed via I/O ports 0xc00/0xc01. */

/*
 * This controls the device -> IRQ routing.
 *
 * Hardcoded IRQs:
 *  0: timer < soc/amd/common/acpi/lpc.asl
 *  1: i8042 - Keyboard
 *  2: cascade
 *  8: rtc0 <- soc/amd/common/acpi/lpc.asl
 *  9: acpi <- soc/amd/common/acpi/lpc.asl
 */

static const struct fch_irq_routing fch_irq_map[] = {
	{ PIRQ_A,	12,		PIRQ_NC },
	{ PIRQ_B,	14,		PIRQ_NC },
	{ PIRQ_C,	15,		PIRQ_NC },
	{ PIRQ_D,	12,		PIRQ_NC },
	{ PIRQ_E,	14,		PIRQ_NC },
	{ PIRQ_F,	15,		PIRQ_NC },
	{ PIRQ_G,	12,		PIRQ_NC },
	{ PIRQ_H,	14,		PIRQ_NC },

	{ PIRQ_SCI,	ACPI_SCI_IRQ,	ACPI_SCI_IRQ },
	{ PIRQ_SDIO,	PIRQ_NC,	PIRQ_NC },
	{ PIRQ_GPIO,	11,		11 },
	{ PIRQ_I2C0,	10,		10 },
	{ PIRQ_I2C1,	 7,		 7 },
	{ PIRQ_I2C2,	 6,		 6 },
	{ PIRQ_I2C3,	 5,		 5 },
	{ PIRQ_UART0,	 4,		 4 },
	{ PIRQ_UART1,	 3,		 3 },

	/* The MISC registers are not interrupt numbers */
	{ PIRQ_MISC,	0xfa,		0x00 },
	{ PIRQ_MISC0,	0x91,		0x00 },
	{ PIRQ_HPET_L,	0x00,		0x00 },
	{ PIRQ_HPET_H,	0x00,		0x00 },
};

const struct fch_irq_routing *mb_get_fch_irq_mapping(size_t *length)
{
	*length = ARRAY_SIZE(fch_irq_map);
	return fch_irq_map;
}

static void mainboard_configure_gpios(void)
{
	size_t num_gpios;
	const struct soc_amd_gpio *gpios;

	baseboard_gpio_table(&gpios, &num_gpios);
	gpio_configure_pads(gpios, num_gpios);
}

static void mainboard_init(void *chip_info)
{
	mainboard_configure_gpios();
	if (CONFIG(BOARD_STARLABS_STARFIGHTER_AMD)) {
		/* The openSIL path does not run the FSP GPP clock setup. */
		uint32_t clocks = misc_read32(GPP_CLK_CNTRL);
		clocks &= ~GPP_CLK_REQ_MASK(GPP_CLK6_REQ_SHIFT);
		clocks |= GPP_CLK_REQ_EXT(GPP_CLK6_REQ_SHIFT);
		misc_write32(GPP_CLK_CNTRL, clocks);

		/* Keep WLAN at ordinary L1; PCI-PM L1.1 wedges its D3hot transition. */
		DEV_PTR(gpp_bridge_2_2)->pcie_l1ss_disable = 0xf;
	}
}

void mainboard_xhci_acpi_fill_ssdt(const struct device *dev)
{
	const struct opregion region = OPREGION("PCFG", PCI_CONFIG, 0, 0x100);
	const uint32_t pci_id = pci_read_config32(dev, PCI_VENDOR_ID);
	static const struct fieldlist fields[] = {
		FIELDLIST_OFFSET(0x54),
		FIELDLIST_NAMESTR("DSTA", 2),
		FIELDLIST_OFFSET(0xc2),
		FIELDLIST_RESERVED(15),
		FIELDLIST_NAMESTR("MSEN", 1),
	};

	if ((dev != DEV_PTR(usb4_xhci_0) && dev != DEV_PTR(usb4_xhci_1)) ||
	    (pci_id != 0x15c01022 && pci_id != 0x15c11022) ||
	    pci_read_config8(dev, 0xc0) != PCI_CAP_ID_MSIX)
		return;

	acpigen_write_store_int_to_namestr(ACPI_DEVICE_SLEEP_D3_HOT, "_S0W");
	acpigen_write_opregion(&region);
	acpigen_write_field("PCFG", fields, ARRAY_SIZE(fields),
		FIELD_WORDACC | FIELD_NOLOCK | FIELD_PRESERVE);
	acpigen_write_method("_PS0", 0);
	acpigen_write_method_end();
	acpigen_write_method_serialized("_PS3", 0);
	acpigen_write_if_lequal_namestr_int("DSTA", ACPI_DEVICE_SLEEP_D3_HOT);
	/* Native PCI resume must re-enable MSI-X to rearm interrupts after D3. */
	acpigen_write_store_int_to_namestr(0, "MSEN");
	acpigen_write_if_end();
	acpigen_write_method_end();
}

static void enable_mainboard(struct device *dev)
{
	dev->ops->acpi_fill_ssdt = merlin_fill_ssdt;
}

struct chip_operations mainboard_ops = {
	.enable_dev = enable_mainboard,
	.init = mainboard_init,
};
