/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <commonlib/helpers.h>
#include <console/console.h>
#include <lib.h>
#include <spi_flash.h>
#include <spi-generic.h>

#include "spi_flash_internal.h"

#define CMD_FM25_WREN		0x06
#define CMD_FM25_WRSR		0x01
#define CMD_FM25_RDSR2		0x35

#define FM25_SR1_BP_SHIFT	2
#define FM25_SR1_BP_MASK	(7 << FM25_SR1_BP_SHIFT)
#define FM25_SR1_TB		BIT(5)
#define FM25_SR1_SEC		BIT(6)
#define FM25_SR1_SRP0		BIT(7)
#define FM25_SR2_SRP1		BIT(0)
#define FM25_SR2_WPS		BIT(3)
#define FM25_SR2_CMP		BIT(6)

#define FM25_FLASH_TIMEOUT_MS	2000

static const struct spi_flash_part_id flash_table[] = {
	{
		/* FM25W128 */
		.id[0] = 0x2818,
		.nr_sectors_shift = 12,
		.fast_read_dual_output_support = 1,
		.fast_read_dual_io_support = 1,
		.protection_granularity_shift = 20,
		.bp_bits = 3,
	},
};

static int fudan_protected_size(const struct spi_flash *flash, u8 bp,
				size_t *protected_size)
{
	if (bp == 1 || bp == 2)
		return -1;
	if (bp == 0)
		*protected_size = 0;
	else if (bp == 7)
		*protected_size = flash->size;
	else
		*protected_size = flash->size >> (7 - bp);

	return 0;
}

static int fudan_read_status(const struct spi_flash *flash, u8 *sr1, u8 *sr2)
{
	int ret = spi_flash_status(flash, sr1);

	if (ret)
		return ret;

	return spi_flash_read_status(flash, CMD_FM25_RDSR2, sr2);
}

static int fudan_bp_to_region(const struct spi_flash *flash, u8 bp, bool tb,
			      bool cmp, struct region *region)
{
	size_t protected_size;

	if (fudan_protected_size(flash, bp, &protected_size))
		return -1;

	if (cmp) {
		protected_size = flash->size - protected_size;
		tb = !tb;
	}

	*region = region_create(tb ? 0 : flash->size - protected_size,
				protected_size);
	return 0;
}

static int fudan_get_write_protection(const struct spi_flash *flash,
				      const struct region *region)
{
	struct region wp_region;
	u8 sr1, sr2;
	int ret;

	ret = fudan_read_status(flash, &sr1, &sr2);
	if (ret)
		return ret;
	if ((sr1 & FM25_SR1_SEC) || (sr2 & FM25_SR2_WPS))
		return -1;

	ret = fudan_bp_to_region(flash,
				 (sr1 & FM25_SR1_BP_MASK) >> FM25_SR1_BP_SHIFT,
				 !!(sr1 & FM25_SR1_TB),
				 !!(sr2 & FM25_SR2_CMP), &wp_region);
	if (ret) {
		printk(BIOS_ERR, "FUDAN: unsupported BP encoding\n");
		return -1;
	}

	if (!region_sz(&wp_region)) {
		printk(BIOS_DEBUG, "FUDAN: flash isn't protected\n");
		return 0;
	}

	printk(BIOS_DEBUG, "FUDAN: flash protected range 0x%08zx-0x%08zx\n",
	       region_offset(&wp_region), region_last(&wp_region));

	return region_is_subregion(&wp_region, region);
}

static int fudan_write_status(const struct spi_flash *flash, u8 sr1, u8 sr2)
{
	u8 verify_sr1, verify_sr2;
	int ret;

	/* FM25W128 permits SR1 and SR2 in one WRSR transaction. */
	if (flash->ops->write_status) {
		const u8 regs[] = { sr1, sr2 };

		ret = spi_flash_write_status(flash, CMD_FM25_WRSR, regs,
					     sizeof(regs));
	} else {
		const struct {
			u8 cmd;
			u8 sr1;
			u8 sr2;
		} __packed command = { CMD_FM25_WRSR, sr1, sr2 };

		ret = spi_flash_cmd(&flash->spi, CMD_FM25_WREN, NULL, 0);
		if (!ret)
			ret = spi_flash_cmd_write(&flash->spi,
						  (const u8 *)&command,
						  sizeof(command),
						  NULL, 0);
		if (!ret)
			ret = spi_flash_cmd_wait_ready(flash,
						       FM25_FLASH_TIMEOUT_MS);
	}
	if (ret)
		return ret;

	ret = spi_flash_status(flash, &verify_sr1);
	if (ret)
		return ret;

	if ((verify_sr1 & (FM25_SR1_BP_MASK | FM25_SR1_TB |
			   FM25_SR1_SEC | FM25_SR1_SRP0)) !=
	    (sr1 & (FM25_SR1_BP_MASK | FM25_SR1_TB |
		    FM25_SR1_SEC | FM25_SR1_SRP0))) {
		printk(BIOS_ERR, "FUDAN: status register 1 verification failed\n");
		return -1;
	}

	ret = spi_flash_read_status(flash, CMD_FM25_RDSR2, &verify_sr2);
	if (ret)
		return ret;

	if (verify_sr2 != sr2) {
		printk(BIOS_ERR, "FUDAN: status register 2 verification failed\n");
		return -1;
	}

	return 0;
}

static int fudan_set_write_protection(const struct spi_flash *flash,
				      const struct region *region,
				      enum spi_flash_status_reg_lockdown mode)
{
	bool tb;
	u8 bp;
	u8 sr1, sr2;
	int ret;

	if (region_offset(region) != 0 && region_last(region) != flash->size - 1)
		return -1;
	if (region_sz(region) > flash->size / 2 && region_sz(region) != flash->size) {
		printk(BIOS_ERR, "FUDAN: complement protection is unavailable\n");
		return -1;
	}

	if (region_sz(region) == 0)
		bp = 0;
	else if (region_sz(region) == flash->size)
		bp = 7;
	else if (IS_POWER_OF_2(region_sz(region)) &&
		 region_sz(region) >= flash->size / 16)
		bp = 7 - log2(flash->size / region_sz(region));
	else
		return -1;

	tb = region_offset(region) == 0;

	ret = fudan_read_status(flash, &sr1, &sr2);
	if (ret)
		return ret;

	sr1 &= ~(FM25_SR1_BP_MASK | FM25_SR1_TB | FM25_SR1_SEC);
	sr1 |= bp << FM25_SR1_BP_SHIFT;
	if (tb)
		sr1 |= FM25_SR1_TB;

	switch (mode) {
	case SPI_WRITE_PROTECTION_PRESERVE:
		break;
	case SPI_WRITE_PROTECTION_NONE:
		sr1 &= ~FM25_SR1_SRP0;
		sr2 &= ~FM25_SR2_SRP1;
		break;
	case SPI_WRITE_PROTECTION_PIN:
		sr1 |= FM25_SR1_SRP0;
		sr2 &= ~FM25_SR2_SRP1;
		break;
	case SPI_WRITE_PROTECTION_REBOOT:
		sr1 &= ~FM25_SR1_SRP0;
		sr2 |= FM25_SR2_SRP1;
		break;
	case SPI_WRITE_PROTECTION_PERMANENT:
		sr1 |= FM25_SR1_SRP0;
		sr2 |= FM25_SR2_SRP1;
		break;
	default:
		return -1;
	}

	/* The requested ranges use the BP/TB scheme without complementing it. */
	sr2 &= ~(FM25_SR2_CMP | FM25_SR2_WPS);

	ret = fudan_write_status(flash, sr1, sr2);
	if (ret)
		return ret;

	printk(BIOS_DEBUG, "FUDAN: write-protection set to range "
	       "0x%08zx-0x%08zx\n", region_offset(region), region_last(region));
	return 0;
}

static const struct spi_flash_protection_ops spi_flash_protection_ops = {
	.get_write = fudan_get_write_protection,
	.set_write = fudan_set_write_protection,
};

const struct spi_flash_vendor_info spi_flash_fudan_vi = {
	.id = VENDOR_ID_FUDAN,
	.page_size_shift = 8,
	.sector_size_kib_shift = 2,
	.match_id_mask[0] = 0xffff,
	.ids = flash_table,
	.nr_part_ids = ARRAY_SIZE(flash_table),
	.desc = &spi_flash_pp_0x20_sector_desc,
	.prot_ops = &spi_flash_protection_ops,
};
