/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <acpi/acpigen.h>
#include <acpi/acpi_gnvs.h>
#include <boot/coreboot_tables.h>
#include <console/uart.h>
#include <cpu/x86/smm.h>
#include <device/device.h>
#include <device/mmio.h>
#include <device/pci.h>
#include <device/pci_ids.h>
#include <device/pci_ops.h>
#include <intelblocks/irq.h>
#include <intelblocks/lpss.h>
#include <intelblocks/uart.h>
#include <soc/pci_devs.h>
#include <soc/iomap.h>
#include <soc/nvs.h>

#define UART_PCI_ENABLE	(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)

extern const unsigned int uart_devices[];
extern const int uart_devices_size;

#if CONFIG(INTEL_LPSS_UART_FOR_CONSOLE) && \
	(CONFIG(SOC_INTEL_ALDERLAKE) || CONFIG(SOC_INTEL_METEORLAKE))
#define UART_SOURCE_CLOCK 100000000ULL
#define UART_CLOCK_CONTROL 0x200
#define UART_CLOCK_ENABLE 1U
#define UART_CLOCK_DIVISOR_MASK 0x7fffU

_Static_assert(CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_M_VAL > 0 &&
	CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_M_VAL <= UART_CLOCK_DIVISOR_MASK,
	"LPSS UART clock numerator must be a nonzero 15-bit value");
_Static_assert(CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_N_VAL > 0 &&
	CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_N_VAL <= UART_CLOCK_DIVISOR_MASK,
	"LPSS UART clock denominator must be a nonzero 15-bit value");
_Static_assert(UART_SOURCE_CLOCK * CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_M_VAL /
	CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_N_VAL > 0 &&
	UART_SOURCE_CLOCK * CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_M_VAL /
	CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_N_VAL <= UINT32_MAX / 2,
	"LPSS UART input clock must fit the baud divisor calculation");

unsigned int uart_platform_refclk(void)
{
	uint32_t numerator = CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_M_VAL;
	uint32_t denominator = CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_N_VAL;
	uint64_t clock;

	/* Outside ramstage, use the configured platform console clock. */
	if (ENV_RAMSTAGE) {
		const struct device *dev = uart_get_device();
		uintptr_t base;
		uint32_t control;

		if (!dev || !dev->enabled ||
		    lpss_get_power_state(PCI_BDF(dev)) != STATE_D0)
			return 0;
		base = pci_read_config32(dev, PCI_BASE_ADDRESS_0) & ~0xFFF;
		if (!base || base != uart_platform_base(CONFIG_UART_FOR_CONSOLE) ||
		    !(pci_read_config16(dev, PCI_COMMAND) & PCI_COMMAND_MEMORY))
			return 0;
		control = read32((void *)(base + LPSS_RESET_CTL_REG));
		if (control == UINT32_MAX || (control & 3U) != 3U)
			return 0;
		control = read32((void *)(base + UART_CLOCK_CONTROL));
		if (control == UINT32_MAX || !(control & UART_CLOCK_ENABLE))
			return 0;
		numerator = (control >> 1) & UART_CLOCK_DIVISOR_MASK;
		denominator = (control >> 16) & UART_CLOCK_DIVISOR_MASK;
		if (!numerator || !denominator)
			return 0;
	}
	clock = UART_SOURCE_CLOCK * numerator / denominator;
	return clock && clock <= UINT32_MAX / 2 ? clock : 0;
}

#if ENV_RAMSTAGE
enum cb_err fill_lb_serial_capability(struct lb_prh_serial_capability *serial)
{
	const struct device *dev = uart_get_device();
	const struct resource *resource;
	uintptr_t base;
	uint32_t clock, parameters, fifo_mode, enabled;

	if (!dev || !dev->enabled || !uart_is_initialized(CONFIG_UART_FOR_CONSOLE) ||
	    pci_read_config16(dev, PCI_VENDOR_ID) != PCI_VID_INTEL)
		return CB_ERR;
	/* Documented on-package ADL and MTL LPSS UART register owners only. */
	switch (pci_read_config16(dev, PCI_DEVICE_ID)) {
	case 0x51a8:
	case 0x51a9:
	case 0x51c7:
	case 0x7e25:
	case 0x7e26:
	case 0x7e52:
		break;
	default:
		return CB_ERR;
	}
	base = uart_platform_base(CONFIG_UART_FOR_CONSOLE);
	resource = probe_resource(dev, PCI_BASE_ADDRESS_0);
	if (!resource || !(resource->flags & IORESOURCE_MEM) ||
	    !(resource->flags & IORESOURCE_ASSIGNED) || resource->base != base ||
	    resource->size < 0x300 || resource->size > UINT32_MAX)
		return CB_ERR;
	/* The shared clock owner validates D0, BAR, decode and reset before MMIO. */
	clock = uart_platform_refclk();
	if (!clock || read32((void *)(base + 0xfc)) != 0x44570110)
		return CB_ERR;
	parameters = read32((void *)(base + 0xf4));
	if (parameters == UINT32_MAX || (parameters & 3U) != 2U ||
	    (parameters & ((1U << 11) | (1U << 12))) != ((1U << 11) | (1U << 12)))
		return CB_ERR;
	fifo_mode = (parameters >> 16) & 0xffU;
	if (!fifo_mode || fifo_mode > 128U || (fifo_mode & (fifo_mode - 1U)))
		return CB_ERR;
	/* SFE is the readable FIFO-enable shadow; never read IIR or write FCR here. */
	enabled = read32((void *)(base + 0x98));
	if (enabled > 1U)
		return CB_ERR;
	*serial = (struct lb_prh_serial_capability) {
		.revision = LB_PRH_SERIAL_CAPABILITY_REVISION,
		.family = LB_PRH_SERIAL_FAMILY_INTEL_LPSS,
		.base = base,
		.input_hertz = clock,
		.register_window = resource->size,
		.fifo_capacity = fifo_mode * 16U,
		.type = LB_SERIAL_TYPE_MEMORY_MAPPED,
		.register_width = sizeof(uint32_t),
		.register_stride = sizeof(uint32_t),
		.flags = LB_PRH_SERIAL_INITIALIZED |
			(enabled ? LB_PRH_SERIAL_FIFO_ENABLED : 0),
	};
	return CB_SUCCESS;
}
#endif
#endif

static void uart_lpss_init(pci_devfn_t dev, uintptr_t baseaddr)
{
	/* Ensure controller is in D0 state */
	lpss_set_power_state(dev, STATE_D0);

	/* Take UART out of reset */
	lpss_reset_release(baseaddr);

	/* Set M and N divisor inputs and enable clock */
	lpss_clk_update(baseaddr, CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_M_VAL,
			CONFIG_SOC_INTEL_COMMON_LPSS_UART_CLK_N_VAL);
}

#if CONFIG(INTEL_LPSS_UART_FOR_CONSOLE)
uintptr_t uart_platform_base(unsigned int idx)
{
	if (idx == CONFIG_UART_FOR_CONSOLE)
		return CONFIG_CONSOLE_UART_BASE_ADDRESS;
	return 0;
}
#endif

static pci_devfn_t uart_console_get_pci_bdf(void)
{
	int devfn;

	/*
	 * This function will get called even if INTEL_LPSS_UART_FOR_CONSOLE
	 * config option is not selected.
	 * By default return NULL in this case to avoid compilation errors.
	 */
	if (!CONFIG(INTEL_LPSS_UART_FOR_CONSOLE))
		return PCI_DEV_INVALID;

	if (CONFIG_UART_FOR_CONSOLE > uart_devices_size)
		return PCI_DEV_INVALID;

	devfn = uart_devices[CONFIG_UART_FOR_CONSOLE];
	if (devfn == PCI_DEVFN_INVALID)
		return PCI_DEV_INVALID;

	return PCI_DEV(0, PCI_SLOT(devfn), PCI_FUNC(devfn));
}

const struct device *uart_get_device(void)
{
	pci_devfn_t dev = uart_console_get_pci_bdf();
	if (dev == PCI_DEV_INVALID)
		return NULL;

	return pcidev_path_on_root(PCI_DEV2DEVFN(dev));
}

bool uart_is_controller_initialized(void)
{
	uintptr_t base;
	pci_devfn_t dev = uart_console_get_pci_bdf();

	if (dev == PCI_DEV_INVALID)
		return false;

	base = pci_s_read_config32(dev, PCI_BASE_ADDRESS_0) & ~0xFFF;
	if (!base)
		return false;

	if (ENV_SMM && smm_points_to_smram((void *)base, 4 * KiB)) {
		printk(BIOS_EMERG, "UART: BAR0 0x%" PRIxPTR " overlaps SMRAM!\n", base);
		die("SMM BAR exploitation attempt detected.");
	}

	if ((pci_s_read_config16(dev, PCI_COMMAND) & UART_PCI_ENABLE)
	    != UART_PCI_ENABLE)
		return false;

	return !lpss_is_controller_in_reset(base);
}

void uart_bootblock_init(void)
{
	const uint32_t baseaddr = CONFIG_CONSOLE_UART_BASE_ADDRESS;
	pci_devfn_t dev = uart_console_get_pci_bdf();

	if (dev == PCI_DEV_INVALID)
		return;

	/* Set UART base address */
	pci_s_write_config32(dev, PCI_BASE_ADDRESS_0, baseaddr);

	/* Enable memory access and bus master */
	pci_s_write_config16(dev, PCI_COMMAND, UART_PCI_ENABLE);

	uart_lpss_init(dev, baseaddr);
}

#if ENV_RAMSTAGE

static void uart_read_resources(struct device *dev)
{
	pci_dev_read_resources(dev);

	/* Set the configured UART base address for the debug port */
	if (CONFIG(INTEL_LPSS_UART_FOR_CONSOLE) &&
	    uart_is_debug_controller(dev)) {
		struct resource *res = find_resource(dev, PCI_BASE_ADDRESS_0);
		/* Need to set the base and size for the resource allocator. */
		res->base = CONFIG_CONSOLE_UART_BASE_ADDRESS;
		res->size = 0x1000;
		res->flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED |
				IORESOURCE_FIXED;
	}
	/* In ACPI mode mark the decoded region as reserved */
	if (dev->hidden) {
		struct resource *res = find_resource(dev, PCI_BASE_ADDRESS_0);
		res->flags |= IORESOURCE_RESERVE;
	}
}

/*
 * Check if UART debug port controller needs to be initialized on resume.
 *
 * Returns:
 * true = when SoC wants debug port initialization on resume
 * false = otherwise
 */
static bool pch_uart_init_debug_controller_on_resume(void)
{
	struct global_nvs *gnvs = acpi_get_gnvs();

	if (gnvs)
		return !!gnvs->uior;

	return false;
}

bool uart_is_debug_controller(struct device *dev)
{
	return dev == uart_get_device();
}

/*
 * This is a workaround to enable UART controller for the debug port if:
 * 1. CONSOLE_SERIAL is not enabled in coreboot, and
 * 2. This boot is S3 resume, and
 * 3. SoC wants to initialize debug UART controller.
 *
 * This workaround is required because Linux kernel hangs on resume if console
 * is not enabled in coreboot, but it is enabled in kernel and not suspended.
 */
static bool uart_controller_needs_init(struct device *dev)
{
	/*
	 * If coreboot has CONSOLE_SERIAL enabled, the skip re-initializing
	 * controller here.
	 */
	if (CONFIG(CONSOLE_SERIAL))
		return false;

	/* If this device does not correspond to debug port, then skip. */
	if (!uart_is_debug_controller(dev))
		return false;

	/* Initialize UART controller only on S3 resume. */
	if (!acpi_is_wakeup_s3())
		return false;

	/*
	 * check if SOC wants to initialize UART on resume
	 */
	return pch_uart_init_debug_controller_on_resume();
}

static void uart_common_enable_resources(struct device *dev)
{
	pci_dev_enable_resources(dev);

	if (uart_controller_needs_init(dev)) {
		uintptr_t base;

		base = pci_read_config32(dev, PCI_BASE_ADDRESS_0) & ~0xFFF;
		if (base)
			uart_lpss_init(PCI_BDF(dev), base);
	}
}

static void uart_acpi_write_irq(const struct device *dev)
{
	if (CONFIG(SOC_INTEL_COMMON_BLOCK_IRQ)) {
		const int irq = get_pci_devfn_irq(dev->path.pci.devfn);
		if (irq != INVALID_IRQ) {
			struct acpi_irq airq = (struct acpi_irq)ACPI_IRQ_LEVEL_LOW(irq);
			acpi_device_write_interrupt(&airq);
		}
	}
}

/*
 * Generate an ACPI entry if the device is enabled in devicetree for the ACPI
 * LPSS driver. In this mode the device and vendor ID reads as 0xffff, but the
 * PCI device is still there.
 */
static void uart_fill_ssdt(const struct device *dev)
{
	const char *scope = acpi_device_scope(dev);
	const char *hid = acpi_device_hid(dev);
	struct resource *res;

	/* In ACPI mode the device is "invisible" */
	if (!dev->hidden)
		return;

	if (!scope || !hid)
		return;

	res = probe_resource(dev, PCI_BASE_ADDRESS_0);
	if (!res)
		return;

	/* Scope */
	acpigen_write_scope(scope);

	/* Device */
	acpigen_write_device(acpi_device_name(dev));
	acpigen_write_name_string("_HID", hid);
	/*
	 * Advertise compatibility to Sunrise Point, as the Linux kernel doesn't support
	 * CannonPoint yet...
	 */
	if (strcmp(hid, "INT34B8") == 0)
		acpigen_write_name_string("_CID", "INT3448");
	else if (strcmp(hid, "INT34B9") == 0)
		acpigen_write_name_string("_CID", "INT3449");
	else if (strcmp(hid, "INT34BA") == 0)
		acpigen_write_name_string("_CID", "INT344A");

	acpi_device_write_uid(dev);
	acpigen_write_name_string("_DDN", "LPSS ACPI UART");

	/* Do not hide the UART device from the OS */
	acpigen_write_STA(ACPI_STATUS_DEVICE_ALL_ON);

	/* Resources */
	acpigen_write_name("_CRS");
	acpigen_write_resourcetemplate_header();

	uart_acpi_write_irq(dev);
	acpigen_write_mem32fixed(1, res->base, res->size);

	acpigen_write_resourcetemplate_footer();

	acpigen_pop_len(); /* Device */
	acpigen_pop_len(); /* Scope */
}

static const char *uart_acpi_hid(const struct device *dev)
{
	switch (dev->device) {
	case PCI_DID_INTEL_APL_UART0:
		return "80865abc";
	case PCI_DID_INTEL_APL_UART1:
		return "80865abe";
	case PCI_DID_INTEL_APL_UART2:
		return "80865ac0";
	case PCI_DID_INTEL_GLK_UART0:
		return  "808631bc";
	case PCI_DID_INTEL_GLK_UART1:
		return  "808631be";
	case PCI_DID_INTEL_GLK_UART2:
		return  "808631c0";
	case PCI_DID_INTEL_GLK_UART3:
		return  "808631ee";
	case PCI_DID_INTEL_SPT_UART0:
	case PCI_DID_INTEL_SPT_H_UART0:
		return "INT3448";
	case PCI_DID_INTEL_SPT_UART1:
	case PCI_DID_INTEL_SPT_H_UART1:
		return "INT3449";
	case PCI_DID_INTEL_SPT_UART2:
	case PCI_DID_INTEL_SPT_H_UART2:
		return "INT344A";
	case PCI_DID_INTEL_CNP_H_UART0:
		return "INT34B8";
	case PCI_DID_INTEL_CNP_H_UART1:
		return "INT34B9";
	case PCI_DID_INTEL_CNP_H_UART2:
		return "INT34BA";
	default:
		return NULL;
	}
}

static const char *uart_acpi_name(const struct device *dev)
{
	switch (dev->device) {
	case PCI_DID_INTEL_NVL_UART0:
	case PCI_DID_INTEL_WCL_UART0:
	case PCI_DID_INTEL_PTL_H_UART0:
	case PCI_DID_INTEL_PTL_U_H_UART0:
	case PCI_DID_INTEL_LNL_UART0:
	case PCI_DID_INTEL_ADP_P_UART0:
	case PCI_DID_INTEL_APL_UART0:
	case PCI_DID_INTEL_GLK_UART0:
	case PCI_DID_INTEL_SPT_UART0:
	case PCI_DID_INTEL_SPT_H_UART0:
	case PCI_DID_INTEL_CNP_H_UART0:
		return "UAR0";
	case PCI_DID_INTEL_NVL_UART1:
	case PCI_DID_INTEL_WCL_UART1:
	case PCI_DID_INTEL_PTL_H_UART1:
	case PCI_DID_INTEL_PTL_U_H_UART1:
	case PCI_DID_INTEL_LNL_UART1:
	case PCI_DID_INTEL_ADP_P_UART1:
	case PCI_DID_INTEL_APL_UART1:
	case PCI_DID_INTEL_GLK_UART1:
	case PCI_DID_INTEL_SPT_UART1:
	case PCI_DID_INTEL_SPT_H_UART1:
	case PCI_DID_INTEL_CNP_H_UART1:
		return "UAR1";
	case PCI_DID_INTEL_NVL_UART2:
	case PCI_DID_INTEL_WCL_UART2:
	case PCI_DID_INTEL_PTL_H_UART2:
	case PCI_DID_INTEL_PTL_U_H_UART2:
	case PCI_DID_INTEL_LNL_UART2:
	case PCI_DID_INTEL_ADP_P_UART2:
	case PCI_DID_INTEL_APL_UART2:
	case PCI_DID_INTEL_GLK_UART2:
	case PCI_DID_INTEL_SPT_UART2:
	case PCI_DID_INTEL_SPT_H_UART2:
	case PCI_DID_INTEL_CNP_H_UART2:
		return "UAR2";
	case PCI_DID_INTEL_ADP_P_UART3:
	case PCI_DID_INTEL_GLK_UART3:
		return "UAR3";
	default:
		return NULL;
	}
}

struct device_operations uart_ops = {
	.read_resources		= uart_read_resources,
	.set_resources		= pci_dev_set_resources,
	.enable_resources	= uart_common_enable_resources,
	.ops_pci		= &pci_dev_ops_pci,
	.acpi_fill_ssdt		= uart_fill_ssdt,
	.acpi_hid		= uart_acpi_hid,
	.acpi_name		= uart_acpi_name,
};

static const unsigned short pci_device_ids[] = {
	PCI_DID_INTEL_LNL_UART0,
	PCI_DID_INTEL_LNL_UART1,
	PCI_DID_INTEL_LNL_UART2,
	PCI_DID_INTEL_MCC_UART0,
	PCI_DID_INTEL_MCC_UART1,
	PCI_DID_INTEL_MCC_UART2,
	0,
};

static const struct pci_driver pch_uart __pci_driver = {
	.ops		= &uart_ops,
	.vendor		= PCI_VID_INTEL,
	.devices	= pci_device_ids,
};
#endif /* ENV_RAMSTAGE */
