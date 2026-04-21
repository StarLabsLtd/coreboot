/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <baseboard/variants.h>
#include <commonlib/helpers.h>
#include <gpio.h>

/*
 * This is intentionally conservative. The StarFighter AMD / F2-A wiring still
 * needs to be mined from the AMI build outputs and schematic, so only keep the
 * pads needed for eSPI EC access, debug UART, and the first obvious PCIe
 * clock/reset lines.
 */
static const struct soc_amd_gpio base_gpio_table[] = {
	/* PWR_BTN_L */
	PAD_NF(GPIO_0, PWR_BTN_L, PULL_NONE),
	/* SYS_RESET_L */
	PAD_NF(GPIO_1, SYS_RESET_L, PULL_NONE),
	/* WAKE_L */
	PAD_NF_SCI(GPIO_2, WAKE_L, PULL_NONE, EDGE_LOW),
	/* AC_PRES */
	PAD_NF(GPIO_23, AC_PRES, PULL_NONE),
	/* eSPI */
	PAD_NF(GPIO_22, ESPI_ALERT_D1, PULL_NONE),
	PAD_NF(GPIO_30, ESPI_CS_L, PULL_NONE),
	PAD_NF(GPIO_68, SPI1_DAT2, PULL_NONE),
	PAD_NF(GPIO_69, SPI1_DAT3, PULL_NONE),
	PAD_NF(GPIO_77, SPI1_CLK, PULL_NONE),
	PAD_NF(GPIO_80, SPI1_DAT1, PULL_NONE),
	PAD_NF(GPIO_81, SPI1_DAT0, PULL_NONE),
	/* Debug UART */
	PAD_NF(GPIO_141, UART0_RXD, PULL_NONE),
	PAD_NF(GPIO_143, UART0_TXD, PULL_NONE),
	/* First obvious CLKREQ lines from the Phoenix routing model */
	PAD_NF(GPIO_92, CLK_REQ0_L, PULL_NONE),
	PAD_NF(GPIO_115, CLK_REQ1_L, PULL_NONE),
	PAD_NF(GPIO_131, CLK_REQ3_L, PULL_NONE),
};

static const struct soc_amd_gpio bootblock_gpio_table[] = {
};

static const struct soc_amd_gpio early_gpio_table[] = {
	/* Keep UART pins native as early as possible for bring-up. */
	PAD_NF(GPIO_141, UART0_RXD, PULL_NONE),
	PAD_NF(GPIO_143, UART0_TXD, PULL_NONE),
};

static const struct soc_amd_gpio romstage_gpio_table[] = {
	/* Tentative WLAN reset and primary NVMe PERST# release. */
	PAD_GPO(GPIO_38, HIGH),
	PAD_NFO(GPIO_26, PCIE_RST0_L, HIGH),
};

static const struct soc_amd_gpio espi_gpio_table[] = {
	PAD_NF(GPIO_22, ESPI_ALERT_D1, PULL_NONE),
	PAD_NF(GPIO_30, ESPI_CS_L, PULL_NONE),
	PAD_NF(GPIO_68, SPI1_DAT2, PULL_NONE),
	PAD_NF(GPIO_69, SPI1_DAT3, PULL_NONE),
	PAD_NF(GPIO_77, SPI1_CLK, PULL_NONE),
	PAD_NF(GPIO_80, SPI1_DAT1, PULL_NONE),
	PAD_NF(GPIO_81, SPI1_DAT0, PULL_NONE),
};

void baseboard_gpio_table(const struct soc_amd_gpio **gpio, size_t *size)
{
	*size = ARRAY_SIZE(base_gpio_table);
	*gpio = base_gpio_table;
}

__weak void baseboard_romstage_gpio_table(const struct soc_amd_gpio **gpio, size_t *size)
{
	*size = ARRAY_SIZE(romstage_gpio_table);
	*gpio = romstage_gpio_table;
}

__weak void variant_bootblock_gpio_table(const struct soc_amd_gpio **gpio, size_t *size)
{
	*size = ARRAY_SIZE(bootblock_gpio_table);
	*gpio = bootblock_gpio_table;
}

__weak void variant_early_gpio_table(const struct soc_amd_gpio **gpio, size_t *size)
{
	*size = ARRAY_SIZE(early_gpio_table);
	*gpio = early_gpio_table;
}

void variant_espi_gpio_table(const struct soc_amd_gpio **gpio, size_t *size)
{
	*size = ARRAY_SIZE(espi_gpio_table);
	*gpio = espi_gpio_table;
}

__weak void variant_tpm_gpio_table(const struct soc_amd_gpio **gpio, size_t *size)
{
	*size = 0;
	*gpio = NULL;
}

__weak void variant_override_gpio_table(const struct soc_amd_gpio **gpio, size_t *size)
{
	*size = 0;
	*gpio = NULL;
}
