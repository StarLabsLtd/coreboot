/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <amdblocks/acpi.h>
#include <amdblocks/amd_pci_util.h>
#include <amdblocks/acpimmio.h>
#include <amdblocks/pci_clk_req.h>
#include <acpi/acpi.h>
#include <arch/io.h>
#include <bootstate.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <power_budget.h>
#include <acpi/acpigen.h>
#include <commonlib/helpers.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pciexp.h>
#include <drivers/amd/opensil/mpio/chip.h>
#include <drivers/usb/pci_xhci/pci_xhci.h>
#include <option.h>
#include <soc/amd/phoenix/chip.h>
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

static void configure_pcie_power_management(struct device *dev, bool clock_pm,
					   unsigned int aspm, unsigned int l1ss)
{
	struct drivers_amd_opensil_mpio_config *config = dev->chip_info;

	dev->disable_pcie_clk_pm = !clock_pm;
	if (!clock_pm) {
		config->clock_pm = 0;
		config->clk_req = CLK_DISABLE;
	}
	if (aspm != PHX_ASPM_DEFAULT) {
		/* CFR values 1..4 map to the MPIO/Link Control states 0..3. */
		config->aspm = aspm - PHX_ASPM_DISABLED;
		dev->pcie_aspm_disable = PCIE_ASPM_BOTH & ~config->aspm;
	}
	if (l1ss != PHX_L1SS_DEFAULT) {
		config->aspm_l1_1 = l1ss != PHX_L1SS_DISABLED;
		config->aspm_l1_2 = l1ss == PHX_L1SS_L1_2;
		dev->pcie_l1ss_disable = 0;
		if (l1ss == PHX_L1SS_DISABLED)
			dev->pcie_l1ss_disable = 0xf;
		else if (l1ss == PHX_L1SS_L1_1)
			dev->pcie_l1ss_disable = 0x5;
	}
}

static void mainboard_init(void *chip_info)
{
	mainboard_configure_gpios();
	if (CONFIG(BOARD_STARLABS_STARFIGHTER_AMD)) {
		struct soc_amd_phoenix_config *soc = config_of_soc();
		/* FP7 presets: 28 W, 45 W, or the existing fused performance policy. */
		static const uint8_t system_config[] = { 2, 5, 0 };
		const unsigned int profile = get_uint_option_checked("power_profile",
			PHX_POWER_PERFORMANCE, OPTION_RANGE(PHX_POWER_SAVER, PHX_POWER_PERFORMANCE));
		soc->system_configuration = system_config[profile];
		const bool wifi_clock_pm = get_uint_option_checked("pciexp_wifi_clk_pm",
								  1, OPTION_BOOL);
		const bool ssd_clock_pm = get_uint_option_checked("pciexp_ssd_clk_pm",
								 1, OPTION_BOOL);
		const unsigned int wifi_aspm = get_uint_option_checked("pciexp_wifi_aspm",
			PHX_ASPM_DEFAULT, OPTION_ENUM(PHX_ASPM_DISABLED, PHX_ASPM_L1,
						     PHX_ASPM_DEFAULT));
		const unsigned int ssd_aspm = get_uint_option_checked("pciexp_ssd_aspm",
			PHX_ASPM_DEFAULT, OPTION_RANGE(PHX_ASPM_DISABLED, PHX_ASPM_DEFAULT));
		const unsigned int ssd_l1ss = get_uint_option_checked("pciexp_ssd_l1ss",
			PHX_L1SS_DEFAULT, OPTION_RANGE(PHX_L1SS_DISABLED, PHX_L1SS_DEFAULT));

		configure_pcie_power_management(DEV_PTR(gpp_bridge_2_2), wifi_clock_pm,
						wifi_aspm, PHX_L1SS_DISABLED);
		configure_pcie_power_management(DEV_PTR(gpp_bridge_1_2), ssd_clock_pm,
						ssd_aspm, ssd_l1ss);
		configure_pcie_power_management(DEV_PTR(gpp_bridge_2_4), ssd_clock_pm,
						ssd_aspm, ssd_l1ss);
		DEV_PTR(gpp_bridge_2_2)->pcie_aspm_disable |= PCIE_ASPM_L0S;

		/* The openSIL path does not run the FSP GPP clock setup. */
		uint32_t clocks = misc_read32(GPP_CLK_CNTRL);
		clocks &= ~GPP_CLK_REQ_MASK(GPP_CLK6_REQ_SHIFT);
		clocks |= wifi_clock_pm ? GPP_CLK_REQ_EXT(GPP_CLK6_REQ_SHIFT) :
					 GPP_CLK_REQ_ON(GPP_CLK6_REQ_SHIFT);
		if (!ssd_clock_pm) {
			clocks &= ~GPP_CLK_REQ_MASK(GPP_CLK3_REQ_SHIFT);
			clocks |= GPP_CLK_REQ_ON(GPP_CLK3_REQ_SHIFT);
		}
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

static void mainboard_fill_ssdt(const struct device *dev)
{
	static const struct fieldlist power_budget_fields[] = {
		FIELDLIST_OFFSET(0x6d), FIELDLIST_NAMESTR("PBVR", 8),
		FIELDLIST_NAMESTR("PBWT", 8),
	};
	merlin_fill_ssdt(dev);
	acpigen_write_scope("\\_SB.PCI0.LPCB.EC");
	acpigen_write_field("ECF2", power_budget_fields, ARRAY_SIZE(power_budget_fields),
			    FIELD_BYTEACC | FIELD_LOCK | FIELD_PRESERVE);
	acpigen_write_method("_Q20", 0);
	acpigen_emit_namestring("\\_SB.PBUD");
	acpigen_write_method_end();
	acpigen_write_scope_end();
}

static void enable_mainboard(struct device *dev)
{
	dev->ops->acpi_fill_ssdt = mainboard_fill_ssdt;
}

struct chip_operations mainboard_ops = {
	.enable_dev = enable_mainboard,
	.init = mainboard_init,
};

static void capture_power_limits(void *unused)
{
	outb(0xff, APM_STS);
	if (apm_control(PHX_POWER_BUDGET_APMC) != CB_SUCCESS ||
	    inb(APM_STS) != PHX_POWER_BUDGET_READY)
		printk(BIOS_ERR, "PHX: native power-limit capture failed; runtime budget disabled\n");
}

BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_LOAD, BS_ON_EXIT, capture_power_limits, NULL);
