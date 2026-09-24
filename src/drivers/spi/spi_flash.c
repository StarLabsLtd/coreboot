/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <assert.h>
#include <boot/coreboot_tables.h>
#include <commonlib/region.h>
#include <console/console.h>
#include <string.h>
#include <spi-generic.h>
#include <spi_flash.h>
#include <timer.h>
#include <types.h>

#include "spi_flash_internal.h"

#if CONFIG(SPI_FLASH_FORCE_4_BYTE_ADDR_MODE)
#define ADDR_MOD 1
#else
#define ADDR_MOD 0
#endif

#define SPI_FIRST_STAGE	\
	(ENV_INITIAL_STAGE || CONFIG(BOOT_DEVICE_MEMORY_MAPPED))

static void spi_flash_addr(u32 addr, u8 *cmd)
{
	/* cmd[0] is actual command */
	if (CONFIG(SPI_FLASH_FORCE_4_BYTE_ADDR_MODE)) {
		cmd[1] = addr >> 24;
		cmd[2] = addr >> 16;
		cmd[3] = addr >> 8;
		cmd[4] = addr >> 0;
	} else {
		cmd[1] = addr >> 16;
		cmd[2] = addr >> 8;
		cmd[3] = addr >> 0;
	}
}

static int do_spi_flash_cmd(const struct spi_slave *spi, const u8 *dout,
			    size_t bytes_out, void *din, size_t bytes_in)
{
	int ret;
	/*
	 * SPI flash requires command-response kind of behavior. Thus, two
	 * separate SPI vectors are required -- first to transmit dout and other
	 * to receive in din. If some specialized SPI flash controllers
	 * (e.g. x86) can perform both command and response together, it should
	 * be handled at SPI flash controller driver level.
	 */
	struct spi_op vectors[] = {
		[0] = { .dout = dout, .bytesout = bytes_out,
			.din = NULL, .bytesin = 0, },
		[1] = { .dout = NULL, .bytesout = 0,
			.din = din, .bytesin = bytes_in },
	};
	size_t count = ARRAY_SIZE(vectors);
	if (!bytes_in)
		count = 1;

	ret = spi_claim_bus(spi);
	if (ret)
		return ret;

	ret = spi_xfer_vector(spi, vectors, count);

	spi_release_bus(spi);
	return ret;
}

static int do_dual_output_cmd(const struct spi_slave *spi, const u8 *dout,
			      size_t bytes_out, void *din, size_t bytes_in)
{
	int ret;

	/*
	 * spi_xfer_vector() will automatically fall back to .xfer() if
	 * .xfer_vector() is unimplemented. So using vector API here is more
	 * flexible, even though a controller that implements .xfer_vector()
	 * and (the non-vector based) .xfer_dual() but not .xfer() would be
	 * pretty odd.
	 */
	struct spi_op vector = { .dout = dout, .bytesout = bytes_out,
				 .din = NULL, .bytesin = 0 };

	ret = spi_claim_bus(spi);
	if (ret)
		return ret;

	ret = spi_xfer_vector(spi, &vector, 1);

	if (!ret)
		ret = spi->ctrlr->xfer_dual(spi, NULL, 0, din, bytes_in);

	spi_release_bus(spi);
	return ret;
}

static int do_dual_io_cmd(const struct spi_slave *spi, const u8 *dout,
			  size_t bytes_out, void *din, size_t bytes_in)
{
	int ret;

	/* Only the very first byte (opcode) is transferred in "single" mode. */
	struct spi_op vector = { .dout = dout, .bytesout = 1,
				 .din = NULL, .bytesin = 0 };

	ret = spi_claim_bus(spi);
	if (ret)
		return ret;

	ret = spi_xfer_vector(spi, &vector, 1);

	if (!ret)
		ret = spi->ctrlr->xfer_dual(spi, &dout[1], bytes_out - 1, NULL, 0);

	if (!ret)
		ret = spi->ctrlr->xfer_dual(spi, NULL, 0, din, bytes_in);

	spi_release_bus(spi);
	return ret;
}

int spi_flash_cmd(const struct spi_slave *spi, u8 cmd, void *response, size_t len)
{
	int ret = do_spi_flash_cmd(spi, &cmd, sizeof(cmd), response, len);
	if (ret)
		printk(BIOS_WARNING, "SF: Failed to send command %02x: %d\n", cmd, ret);

	return ret;
}

int spi_flash_cmd_multi(const struct spi_slave *spi, const u8 *dout, size_t bytes_out,
			void *din, size_t bytes_in)
{
	int ret = do_spi_flash_cmd(spi, dout, bytes_out, din, bytes_in);
	if (ret)
		printk(BIOS_WARNING, "SF: Failed to send command %02x: %d\n", dout[0], ret);

	return ret;
}

/* TODO: This code is quite possibly broken and overflowing stacks. Fix ASAP! */
#pragma GCC diagnostic push
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wstack-usage="
#endif
#pragma GCC diagnostic ignored "-Wvla"
int spi_flash_cmd_write(const struct spi_slave *spi, const u8 *cmd,
			size_t cmd_len, const void *data, size_t data_len)
{
	int ret;
	u8 buff[cmd_len + data_len];
	memcpy(buff, cmd, cmd_len);
	memcpy(buff + cmd_len, data, data_len);

	ret = do_spi_flash_cmd(spi, buff, cmd_len + data_len, NULL, 0);
	if (ret) {
		printk(BIOS_WARNING, "SF: Failed to send write command (%zu bytes): %d\n",
				data_len, ret);
	}

	return ret;
}
#pragma GCC diagnostic pop

/* Perform the read operation honoring spi controller fifo size, reissuing
 * the read command until the full request completed. */
int spi_flash_cmd_read(const struct spi_flash *flash, u32 offset,
				  size_t len, void *buf)
{
	u8 cmd[5 + ADDR_MOD];
	int ret, cmd_len;
	int (*do_cmd)(const struct spi_slave *spi, const u8 *din,
		      size_t in_bytes, void *out, size_t out_bytes);

	if (CONFIG(SPI_FLASH_NO_FAST_READ)) {
		cmd_len = 4 + ADDR_MOD;
		cmd[0] = CMD_READ_ARRAY_SLOW;
		do_cmd = do_spi_flash_cmd;
	} else if (flash->flags.dual_io && flash->spi.ctrlr->xfer_dual) {
		cmd_len = 5 + ADDR_MOD;
		cmd[0] = CMD_READ_FAST_DUAL_IO;
		cmd[4 + ADDR_MOD] = 0;
		do_cmd = do_dual_io_cmd;
	} else if (flash->flags.dual_output && flash->spi.ctrlr->xfer_dual) {
		cmd_len = 5 + ADDR_MOD;
		cmd[0] = CMD_READ_FAST_DUAL_OUTPUT;
		cmd[4 + ADDR_MOD] = 0;
		do_cmd = do_dual_output_cmd;
	} else {
		cmd_len = 5 + ADDR_MOD;
		cmd[0] = CMD_READ_ARRAY_FAST;
		cmd[4 + ADDR_MOD] = 0;
		do_cmd = do_spi_flash_cmd;
	}

	uint8_t *data = buf;
	while (len) {
		size_t xfer_len = spi_crop_chunk(&flash->spi, cmd_len, len);
		spi_flash_addr(offset, cmd);
		ret = do_cmd(&flash->spi, cmd, cmd_len, data, xfer_len);
		if (ret) {
			printk(BIOS_WARNING,
			       "SF: Failed to send read command %#.2x(%#x, %#zx): %d\n",
			       cmd[0], offset, xfer_len, ret);
			return ret;
		}
		offset += xfer_len;
		data += xfer_len;
		len -= xfer_len;
	}

	return 0;
}

int spi_flash_cmd_poll_bit(const struct spi_flash *flash, unsigned long timeout,
			   u8 cmd, u8 poll_bit)
{
	const struct spi_slave *spi = &flash->spi;
	int ret;
	int attempt = 0;
	u8 status;
	struct stopwatch sw;

	stopwatch_init_msecs_expire(&sw, timeout);
	do {
		attempt++;

		ret = do_spi_flash_cmd(spi, &cmd, 1, &status, 1);
		if (ret) {
			printk(BIOS_WARNING,
			       "SF: SPI command failed on attempt %d with rc %d\n", attempt,
			       ret);
			return -1;
		}

		if ((status & poll_bit) == 0)
			return 0;
	} while (!stopwatch_expired(&sw));

	printk(BIOS_WARNING, "SF: timeout at %lld msec after %d attempts\n",
	       stopwatch_duration_msecs(&sw), attempt);

	return -1;
}

int spi_flash_cmd_wait_ready(const struct spi_flash *flash,
			unsigned long timeout)
{
	return spi_flash_cmd_poll_bit(flash, timeout,
		CMD_READ_STATUS, STATUS_WIP);
}

/*
 * Find a SPI flash block erase command from SFDP that fits the constrains.
 *
 * @param flash   Pointer to struct spi_flash
 * @param offset  Offset in bytes from start of SPI flash to start of region to erase
 * @param end     Offset in bytes from start of SPI flash to end of region to erase
 *
 * @return Pointer to struct sfdp_block_erase_info, NULL if not suitable SFDP block was found
 */
static const struct sfdp_block_erase_info *
spi_flash_sfdp_erase_block(const struct sfdp_jedec_info *sfdp, const u32 offset, const u32 end)
{
	const struct sfdp_block_erase_info *info, *best = NULL;
	for (int i = 0; i < ARRAY_SIZE(sfdp->erase_info); i++) {
		info = &sfdp->erase_info[i];
		if (!info->block_size_pow2)
			break;
		const uint32_t bs = (1U << info->block_size_pow2);
		if ((offset + bs) > end)
			continue;
		if (!IS_ALIGNED(offset, bs))
			continue;
		if (best && best->block_size_pow2 > info->block_size_pow2)
			continue;
		best = info;
	}
	return best;
}

int spi_flash_cmd_erase(const struct spi_flash *flash, u32 offset, size_t len)
{
	u32 start, end, erase_size;
	int ret = -1;
	u8 cmd[4 + ADDR_MOD], erase_cmd;
	struct sfdp_jedec_info info = {0};

	erase_size = flash->sector_size;
	if (offset % erase_size || len % erase_size) {
		printk(BIOS_WARNING, "SF: Erase offset/length not multiple of erase size\n");
		return -1;
	}
	if (len == 0) {
		printk(BIOS_WARNING, "SF: Erase length cannot be 0\n");
		return -1;
	}

	/*
	 * Only parse SFDP when necessary since reading it in is slow.
	 * Using 33Mhz SPI bus frequency it takes about 150 usec to read it.
	 *
	 * The SFDP JEDEC info isn't cached as 'struct spi_flash' is read only here,
	 * and erasing the flash isn't usually done as part of the boot.
	 * On MT25QU256ABA using SFDP results in using 64KiB erase block sizes
	 * over 4KiB blocks and thus reduces boot time by 67msec for each erased block.
	 * The time to read SFDP, every time this function is called, is thus negligible.
	 */
	if (CONFIG(SPI_FLASH_SFDP) && (len >= 2 * erase_size)) {
		if (spi_flash_get_sfdp_info(flash, &info) == CB_SUCCESS) {
			for (int i = 0; i < ARRAY_SIZE(info.erase_info); i++) {
				if (!info.erase_info[i].block_size_pow2)
					continue;
				printk(BIOS_DEBUG, "SF: Erase block size 0x%08x, OP=0x%02x\n",
				       1U << info.erase_info[i].block_size_pow2,
				       info.erase_info[i].opcode);
			}
		}
	}

	erase_cmd = flash->erase_cmd;
	start = offset;
	end = start + len;

	while (offset < end) {
		spi_flash_addr(offset, cmd);

		/*
		 * Try to find a better suited erase op code when SFDP is
		 * available and spi_flash_get_sfdp_info() was successful.
		 */
		if (CONFIG(SPI_FLASH_SFDP) &&
		    info.erase_info[0].block_size_pow2) {
			const struct sfdp_block_erase_info *best =
					spi_flash_sfdp_erase_block(&info, offset, end);
			if (best) {
				erase_size = 1U << best->block_size_pow2;
				erase_cmd = best->opcode;
			} else {
				erase_size = flash->sector_size;
				erase_cmd = flash->erase_cmd;
			}
		}

		cmd[0] = erase_cmd;
		offset += erase_size;

		if (CONFIG(DEBUG_SPI_FLASH)) {
			if (ADDR_MOD)
				printk(BIOS_SPEW, "SF: erase %2x %2x %2x %2x %2x (%x)\n",
					cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], offset);
			else
				printk(BIOS_SPEW, "SF: erase %2x %2x %2x %2x (%x)\n",
					cmd[0], cmd[1], cmd[2], cmd[3], offset);
		}

		ret = spi_flash_cmd(&flash->spi, CMD_WRITE_ENABLE, NULL, 0);
		if (ret)
			goto out;

		ret = spi_flash_cmd_write(&flash->spi, cmd, sizeof(cmd), NULL, 0);
		if (ret)
			goto out;

		ret = spi_flash_cmd_wait_ready(flash,
				SPI_FLASH_PAGE_ERASE_TIMEOUT_MS);
		if (ret)
			goto out;
	}

	printk(BIOS_DEBUG, "SF: Successfully erased %zu bytes @ %#x\n", len, start);

out:
	return ret;
}

int spi_flash_cmd_status(const struct spi_flash *flash, u8 *reg)
{
	return spi_flash_cmd(&flash->spi, flash->status_cmd, reg, sizeof(*reg));
}

int spi_flash_cmd_write_page_program(const struct spi_flash *flash, u32 offset,
				size_t len, const void *buf)
{
	unsigned long byte_addr;
	unsigned long page_size;
	size_t chunk_len;
	size_t actual;
	int ret = 0;
	u8 cmd[4 + ADDR_MOD];

	page_size = flash->page_size;
	cmd[0] = flash->pp_cmd;

	for (actual = 0; actual < len; actual += chunk_len) {
		byte_addr = offset % page_size;
		chunk_len = MIN(len - actual, page_size - byte_addr);
		chunk_len = spi_crop_chunk(&flash->spi, sizeof(cmd), chunk_len);

		spi_flash_addr(offset, cmd);
		if (CONFIG(DEBUG_SPI_FLASH)) {
			if (ADDR_MOD)
				printk(BIOS_SPEW,
					"PP: %p => cmd = { 0x%02x 0x%02x%02x%02x%02x } chunk_len = %zu\n",
					buf + actual, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4],
					chunk_len);
			else
				printk(BIOS_SPEW,
					"PP: %p => cmd = { 0x%02x 0x%02x%02x%02x } chunk_len = %zu\n",
					buf + actual, cmd[0], cmd[1], cmd[2], cmd[3],
					chunk_len);
		}

		ret = spi_flash_cmd(&flash->spi, flash->wren_cmd, NULL, 0);
		if (ret < 0) {
			printk(BIOS_WARNING, "SF: Enabling Write failed\n");
			goto out;
		}

		ret = spi_flash_cmd_write(&flash->spi, cmd, sizeof(cmd),
				buf + actual, chunk_len);
		if (ret < 0) {
			printk(BIOS_WARNING, "SF: Page Program failed\n");
			goto out;
		}

		ret = spi_flash_cmd_wait_ready(flash, SPI_FLASH_PROG_TIMEOUT_MS);
		if (ret)
			goto out;

		offset += chunk_len;
	}

	if (CONFIG(DEBUG_SPI_FLASH))
		printk(BIOS_SPEW, "SF: : Successfully programmed %zu bytes @ 0x%lx\n",
			len, (unsigned long)(offset - len));
	ret = 0;

out:
	return ret;
}

static const struct spi_flash_vendor_info *spi_flash_vendors[] = {
#if CONFIG(SPI_FLASH_ADESTO)
	&spi_flash_adesto_vi,
#endif
#if CONFIG(SPI_FLASH_AMIC)
	&spi_flash_amic_vi,
#endif
#if CONFIG(SPI_FLASH_ATMEL)
	&spi_flash_atmel_vi,
#endif
#if CONFIG(SPI_FLASH_EON)
	&spi_flash_eon_vi,
#endif
#if CONFIG(SPI_FLASH_GIGADEVICE)
	&spi_flash_gigadevice_vi,
#endif
#if CONFIG(SPI_FLASH_MACRONIX)
	&spi_flash_macronix_vi,
#endif
#if CONFIG(SPI_FLASH_SPANSION)
	&spi_flash_spansion_ext1_vi,
	&spi_flash_spansion_ext2_vi,
	&spi_flash_spansion_vi,
#endif
#if CONFIG(SPI_FLASH_SST)
	&spi_flash_sst_ai_vi,
	&spi_flash_sst_vi,
#endif
#if CONFIG(SPI_FLASH_STMICRO)
	&spi_flash_stmicro1_vi,
	&spi_flash_stmicro2_vi,
	&spi_flash_stmicro3_vi,
	&spi_flash_stmicro4_vi,
#endif
#if CONFIG(SPI_FLASH_WINBOND)
	&spi_flash_winbond_vi,
#endif
#if CONFIG(SPI_FLASH_ISSI)
	&spi_flash_issi_vi,
#endif
};
#define IDCODE_LEN 5

static int fill_spi_flash(const struct spi_slave *spi, struct spi_flash *flash,
	const struct spi_flash_vendor_info *vi,
	const struct spi_flash_part_id *part)
{
	memcpy(&flash->spi, spi, sizeof(*spi));
	flash->vendor = vi->id;
	flash->model = part->id[0];

	flash->page_size = 1U << vi->page_size_shift;
	flash->sector_size = (1U << vi->sector_size_kib_shift) * KiB;
	flash->size = flash->sector_size * (1U << part->nr_sectors_shift);
	flash->erase_cmd = vi->desc->erase_cmd;
	flash->status_cmd = vi->desc->status_cmd;
	flash->pp_cmd = vi->desc->pp_cmd;
	flash->wren_cmd = vi->desc->wren_cmd;

	flash->flags.dual_output = part->fast_read_dual_output_support;
	flash->flags.dual_io = part->fast_read_dual_io_support;

	flash->ops = &vi->desc->ops;
	flash->prot_ops = vi->prot_ops;
	flash->part = part;

	if (vi->after_probe)
		return vi->after_probe(flash);

	return 0;
}

static const struct spi_flash_part_id *find_part(const struct spi_flash_vendor_info *vi,
						uint16_t id[2])
{
	size_t i;
	const uint16_t lid[2] = {
		[0] = id[0] & vi->match_id_mask[0],
		[1] = id[1] & vi->match_id_mask[1],
	};

	for (i = 0; i < vi->nr_part_ids; i++) {
		const struct spi_flash_part_id *part = &vi->ids[i];

		if (part->id[0] == lid[0] && part->id[1] == lid[1])
			return part;
	}

	return NULL;
}

static int find_match(const struct spi_slave *spi, struct spi_flash *flash,
			uint8_t manuf_id, uint16_t id[2])
{
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(spi_flash_vendors); i++) {
		const struct spi_flash_vendor_info *vi;
		const struct spi_flash_part_id *part;

		vi = spi_flash_vendors[i];

		if (manuf_id != vi->id)
			continue;

		part = find_part(vi, id);

		if (part == NULL)
			continue;

		return fill_spi_flash(spi, flash, vi, part);
	}

	printk(BIOS_WARNING, "SF: no match for ID %04x %04x\n", id[0], id[1]);
	return -1;
}

int spi_flash_generic_probe(const struct spi_slave *spi,
				struct spi_flash *flash)
{
	int ret, i;
	u8 idcode[IDCODE_LEN];
	u8 manuf_id;
	u16 id[2];

	/* Read the ID codes */
	ret = spi_flash_cmd(spi, CMD_READ_ID, idcode, sizeof(idcode));
	if (ret)
		return -1;

	if (CONFIG(DEBUG_SPI_FLASH)) {
		printk(BIOS_SPEW, "SF: Got idcode: ");
		for (i = 0; i < sizeof(idcode); i++)
			printk(BIOS_SPEW, "%02x ", idcode[i]);
		printk(BIOS_SPEW, "\n");
	}

	manuf_id = idcode[0];

	printk(BIOS_INFO, "Manufacturer: %02x\n", manuf_id);

	/* If no result from RDID command and STMicro parts are enabled attempt
	   to wake the part from deep sleep and obtain alternative id info. */
	if (CONFIG(SPI_FLASH_STMICRO) && manuf_id == 0xff) {
		if (stmicro_release_deep_sleep_identify(spi, idcode))
			return -1;
		manuf_id = idcode[0];
	}

	id[0] = (idcode[1] << 8) | idcode[2];
	id[1] = (idcode[3] << 8) | idcode[4];

	return find_match(spi, flash, manuf_id, id);
}

int spi_flash_probe(unsigned int bus, unsigned int cs, struct spi_flash *flash)
{
	struct spi_slave spi;
	int ret = -1;

	if (spi_setup_slave(bus, cs, &spi)) {
		printk(BIOS_WARNING, "SF: Failed to set up slave\n");
		return -1;
	}

	/* Try special programmer probe if any. */
	if (spi.ctrlr->flash_probe)
		ret = spi.ctrlr->flash_probe(&spi, flash);

	/* If flash is not found, try generic spi flash probe. */
	if (ret)
		ret = spi_flash_generic_probe(&spi, flash);

	/* Give up -- nothing more to try if flash is not found. */
	if (ret) {
		printk(BIOS_WARNING, "SF: Unsupported manufacturer!\n");
		return -1;
	}

	const char *mode_string = "";
	if (flash->flags.dual_io && spi.ctrlr->xfer_dual)
		mode_string = " (Dual I/O mode)";
	else if (flash->flags.dual_output && spi.ctrlr->xfer_dual)
		mode_string = " (Dual Output mode)";
	printk(BIOS_INFO,
	       "SF: Detected %02x %04x with sector size 0x%x, total 0x%x%s\n",
		flash->vendor, flash->model, flash->sector_size, flash->size, mode_string);
	if (bus == CONFIG_BOOT_DEVICE_SPI_FLASH_BUS
			&& flash->size != CONFIG_ROM_SIZE) {
		printk(BIOS_ERR, "SF size 0x%x does not correspond to"
			" CONFIG_ROM_SIZE 0x%x!!\n", flash->size,
			CONFIG_ROM_SIZE);
	}

	if (CONFIG(SPI_FLASH_FORCE_4_BYTE_ADDR_MODE) && SPI_FIRST_STAGE) {
		printk(BIOS_DEBUG, "SF: Entering 4-byte addressing mode\n");
		spi_flash_cmd(&flash->spi, CMD_ENTER_4BYTE_ADDR_MODE, NULL, 0);
	}

	if (CONFIG(SPI_FLASH_EXIT_4_BYTE_ADDR_MODE) && SPI_FIRST_STAGE) {
		printk(BIOS_DEBUG, "SF: Exiting 4-byte addressing mode\n");
		spi_flash_cmd(&flash->spi, CMD_EXIT_4BYTE_ADDR_MODE, NULL, 0);
	}

	/* TODO: only do this in stages that will need to call those functions? */
	if (CONFIG(SPI_FLASH_RPMC)) {
		spi_flash_fill_rpmc_caps(flash);
	}

	return 0;
}

#define VOLATILE_LEASE_CHECK_DOMAIN ((uintptr_t)0x9d8737c52a41b6e3ULL)

enum volatile_lease_state {
	VOLATILE_LEASE_IDLE = 0,
	VOLATILE_LEASE_ACTIVE,
	VOLATILE_LEASE_BUSY,
	VOLATILE_LEASE_POISONED,
};

static struct {
	uint32_t lock;
	uint32_t group_count;
	uint32_t read_count;
	bool boundary;
	enum volatile_lease_state lease_state;
	uintptr_t next_cookie;
	uintptr_t cookie;
	const struct spi_flash *flash;
	const struct spi_flash_ops *ops;
	const struct spi_flash_volatile_lease *owner;
	int (*read)(const struct spi_flash *flash, u32 offset, size_t len,
		void *buf);
	int (*write)(const struct spi_flash *flash, u32 offset, size_t len,
		const void *buf);
	int (*erase)(const struct spi_flash *flash, u32 offset, size_t len);
	int (*status)(const struct spi_flash *flash, u8 *reg);
} volatile_state;

static bool volatile_lock(void)
{
	uint32_t expected = 0;

	return __atomic_compare_exchange_n(&volatile_state.lock, &expected, 1,
		false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void volatile_unlock(void)
{
	__atomic_store_n(&volatile_state.lock, 0, __ATOMIC_RELEASE);
}

static void volatile_lock_wait(void)
{
	while (!volatile_lock())
		;
}

static int volatile_read_begin(void)
{
	int ret = -1;

	if (!volatile_lock())
		return ret;
	if (!volatile_state.boundary &&
	    volatile_state.lease_state == VOLATILE_LEASE_IDLE &&
	    volatile_state.read_count != UINT32_MAX) {
		volatile_state.read_count++;
		ret = 0;
	}
	volatile_unlock();
	return ret;
}

static int volatile_read_end(void)
{
	int ret = -1;

	volatile_lock_wait();
	assert(volatile_state.read_count != 0);
	if (volatile_state.read_count) {
		volatile_state.read_count--;
		ret = 0;
	}
	volatile_unlock();
	return ret;
}

int spi_flash_read(const struct spi_flash *flash, u32 offset, size_t len,
		void *buf)
{
	int ret;

	if (!CONFIG(SPI_FLASH_VOLATILE_LEASE))
		return flash->ops->read(flash, offset, len, buf);
	if (volatile_read_begin())
		return -1;
	ret = flash->ops->read(flash, offset, len, buf);
	if (volatile_read_end())
		return -1;

	return ret;
}

int spi_flash_write(const struct spi_flash *flash, u32 offset, size_t len,
		const void *buf)
{
	int ret;

	if (spi_flash_volatile_group_begin(flash))
		return -1;

	ret = flash->ops->write(flash, offset, len, buf);

	if (spi_flash_volatile_group_end(flash))
		return -1;

	return ret;
}

int spi_flash_erase(const struct spi_flash *flash, u32 offset, size_t len)
{
	int ret;

	if (spi_flash_volatile_group_begin(flash))
		return -1;

	ret = flash->ops->erase(flash, offset, len);

	if (spi_flash_volatile_group_end(flash))
		return -1;

	return ret;
}

int spi_flash_status(const struct spi_flash *flash, u8 *reg)
{
	if (flash->ops->status)
		return flash->ops->status(flash, reg);

	return -1;
}

int spi_flash_is_write_protected(const struct spi_flash *flash,
				 const struct region *region)
{
	struct region flash_region;

	if (!flash || !region)
		return -1;

	flash_region = region_create(0, flash->size);

	if (!region_is_subregion(&flash_region, region))
		return -1;

	if (!flash->prot_ops) {
		printk(BIOS_WARNING, "SPI: Write-protection gathering not "
		       "implemented for this vendor.\n");
		return -1;
	}

	return flash->prot_ops->get_write(flash, region);
}

int spi_flash_set_write_protected(const struct spi_flash *flash,
				  const struct region *region,
				  const enum spi_flash_status_reg_lockdown mode)
{
	struct region flash_region;
	int ret;

	if (!flash)
		return -1;

	flash_region = region_create(0, flash->size);

	if (!region_is_subregion(&flash_region, region))
		return -1;

	if (!flash->prot_ops) {
		printk(BIOS_WARNING, "SPI: Setting write-protection is not "
		       "implemented for this vendor.\n");
		return -1;
	}

	ret = flash->prot_ops->set_write(flash, region, mode);

	if (ret == 0 && mode != SPI_WRITE_PROTECTION_PRESERVE) {
		printk(BIOS_INFO, "SPI: SREG lock-down was set to ");
		switch (mode) {
		case SPI_WRITE_PROTECTION_NONE:
			printk(BIOS_INFO, "NEVER\n");
		break;
		case SPI_WRITE_PROTECTION_PIN:
			printk(BIOS_INFO, "WP\n");
		break;
		case SPI_WRITE_PROTECTION_REBOOT:
			printk(BIOS_INFO, "REBOOT\n");
		break;
		case SPI_WRITE_PROTECTION_PERMANENT:
			printk(BIOS_INFO, "PERMANENT\n");
		break;
		default:
			printk(BIOS_INFO, "UNKNOWN\n");
		break;
		}
	}

	return ret;
}

#if ENV_TEST
uint32_t spi_flash_volatile_group_test_exchange_count(uint32_t count)
{
	uint32_t previous;

	if (!volatile_lock())
		return UINT32_MAX;
	previous = volatile_state.group_count;
	volatile_state.group_count = count;
	volatile_unlock();
	return previous;
}
#endif

int spi_flash_volatile_group_begin(const struct spi_flash *flash)
{
	uint32_t count;
	int ret = 0;

	if (!CONFIG(SPI_FLASH_VOLATILE_LEASE)) {
		if (!CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP))
			return 0;
		count = volatile_state.group_count;
		if (count == UINT32_MAX)
			return -1;
		if (!count) {
			ret = chipset_volatile_group_begin(flash);
			if (ret)
				return ret;
		}
		volatile_state.group_count = count + 1;
		return 0;
	}
	if (!volatile_lock())
		return -1;
	if (volatile_state.boundary ||
	    volatile_state.lease_state != VOLATILE_LEASE_IDLE) {
		volatile_unlock();
		return -1;
	}
	count = volatile_state.group_count;
	if (count == UINT32_MAX)
		goto fail;
	if (count) {
		volatile_state.group_count = count + 1;
		volatile_unlock();
		return 0;
	}
	volatile_state.boundary = 1;
	volatile_unlock();
	if (CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP))
		ret = chipset_volatile_group_begin(flash);
	volatile_lock_wait();
	volatile_state.boundary = 0;
	if (ret)
		goto fail;
	volatile_state.group_count = 1;
	volatile_unlock();
	return 0;
fail:
	volatile_unlock();
	return ret ? ret : -1;
}

int spi_flash_volatile_group_end(const struct spi_flash *flash)
{
	uint32_t count;
	int ret = 0;

	if (!CONFIG(SPI_FLASH_VOLATILE_LEASE)) {
		if (!CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP))
			return 0;
		count = volatile_state.group_count;
		assert(count != 0);
		if (!count)
			return -1;
		if (count > 1) {
			volatile_state.group_count = count - 1;
			return 0;
		}
		ret = chipset_volatile_group_end(flash);
		volatile_state.group_count = 0;
		return ret;
	}
	if (!volatile_lock())
		return -1;
	if (volatile_state.boundary ||
	    volatile_state.lease_state != VOLATILE_LEASE_IDLE) {
		volatile_unlock();
		return -1;
	}
	count = volatile_state.group_count;
	assert(count != 0);
	if (count == 0) {
		volatile_unlock();
		return -1;
	}
	if (count > 1) {
		volatile_state.group_count = count - 1;
		volatile_unlock();
		return 0;
	}

	volatile_state.boundary = 1;
	volatile_unlock();
	if (CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP))
		ret = chipset_volatile_group_end(flash);
	volatile_lock_wait();
	/* The matching logical ownership ends even when chipset cleanup fails. */
	volatile_state.group_count = 0;
	volatile_state.boundary = 0;
	volatile_unlock();

	return ret;
}

static bool volatile_lease_handle_valid(
	const struct spi_flash_volatile_lease *lease)
{
	return lease == volatile_state.owner &&
		lease->private_data[0] == volatile_state.cookie &&
		lease->private_data[1] ==
			(volatile_state.cookie ^ VOLATILE_LEASE_CHECK_DOMAIN) &&
		lease->private_data[2] == (uintptr_t)volatile_state.flash &&
		lease->private_data[3] ==
			((uintptr_t)lease ^ volatile_state.cookie ^
			 VOLATILE_LEASE_CHECK_DOMAIN);
}

static bool volatile_lease_callbacks_valid(void)
{
	return volatile_state.flash && volatile_state.flash->ops ==
		volatile_state.ops && volatile_state.ops &&
		volatile_state.ops->read == volatile_state.read &&
		volatile_state.ops->write == volatile_state.write &&
		volatile_state.ops->erase == volatile_state.erase &&
		volatile_state.ops->status == volatile_state.status;
}

static int volatile_lease_enter(const struct spi_flash *flash,
	const struct spi_flash_volatile_lease *lease)
{
	if (!volatile_lock())
		return -1;
	if (volatile_state.lease_state != VOLATILE_LEASE_ACTIVE ||
	    flash != volatile_state.flash ||
	    !volatile_lease_handle_valid(lease) ||
	    !volatile_lease_callbacks_valid()) {
		if (lease == volatile_state.owner)
			volatile_state.lease_state = VOLATILE_LEASE_POISONED;
		volatile_unlock();
		return -1;
	}
	volatile_state.lease_state = VOLATILE_LEASE_BUSY;
	volatile_unlock();
	return 0;
}

static int volatile_lease_leave(int callback_result)
{
	volatile_lock_wait();
	if (!volatile_lease_handle_valid(volatile_state.owner) ||
	    !volatile_lease_callbacks_valid() ||
	    volatile_state.lease_state != VOLATILE_LEASE_BUSY) {
		volatile_state.lease_state = VOLATILE_LEASE_POISONED;
		callback_result = -1;
	} else
		volatile_state.lease_state = VOLATILE_LEASE_ACTIVE;
	volatile_unlock();
	return callback_result;
}

static bool volatile_lease_span_valid(const struct spi_flash *flash,
	u32 offset, size_t len)
{
	return flash && len && offset <= flash->size &&
		len <= flash->size - offset;
}

int spi_flash_volatile_lease_begin(const struct spi_flash *flash,
	struct spi_flash_volatile_lease *lease)
{
	const struct spi_flash_ops *ops;
	int (*read)(const struct spi_flash *active_flash, u32 offset, size_t len,
		void *buf);
	int (*write)(const struct spi_flash *active_flash, u32 offset, size_t len,
		const void *buf);
	int (*erase)(const struct spi_flash *active_flash, u32 offset, size_t len);
	int (*status)(const struct spi_flash *active_flash, u8 *reg);
	uintptr_t cookie;
	int ret = 0;

	if (!CONFIG(SPI_FLASH_VOLATILE_LEASE) || !flash || !flash->ops ||
	    !flash->ops->read || !flash->ops->write ||
	    !flash->ops->erase || !lease ||
	    memcmp(lease, &(struct spi_flash_volatile_lease){ 0 },
		    sizeof(*lease)) || !volatile_lock())
		return -1;
	if (volatile_state.lease_state != VOLATILE_LEASE_IDLE ||
	    volatile_state.boundary || volatile_state.group_count ||
	    volatile_state.read_count ||
	    volatile_state.next_cookie == UINTPTR_MAX)
		goto fail;
	ops = flash->ops;
	read = ops->read;
	write = ops->write;
	erase = ops->erase;
	status = ops->status;
	if (CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP)) {
		volatile_state.boundary = 1;
		volatile_unlock();
		ret = chipset_volatile_group_begin(flash);
		volatile_lock_wait();
		volatile_state.boundary = 0;
		if (ret)
			goto fail;
		if (memcmp(lease, &(struct spi_flash_volatile_lease){ 0 },
			   sizeof(*lease)) || flash->ops != ops ||
		    ops->read != read || ops->write != write ||
		    ops->erase != erase || ops->status != status) {
			volatile_state.boundary = 1;
			volatile_unlock();
			(void)chipset_volatile_group_end(flash);
			volatile_lock_wait();
			volatile_state.boundary = 0;
			goto fail;
		}
	}
	cookie = ++volatile_state.next_cookie;
	volatile_state.cookie = cookie;
	volatile_state.flash = flash;
	volatile_state.ops = ops;
	volatile_state.owner = lease;
	volatile_state.read = read;
	volatile_state.write = write;
	volatile_state.erase = erase;
	volatile_state.status = status;
	lease->private_data[0] = cookie;
	lease->private_data[1] = cookie ^ VOLATILE_LEASE_CHECK_DOMAIN;
	lease->private_data[2] = (uintptr_t)flash;
	lease->private_data[3] = (uintptr_t)lease ^ cookie ^
		VOLATILE_LEASE_CHECK_DOMAIN;
	volatile_state.lease_state = VOLATILE_LEASE_ACTIVE;
	volatile_unlock();
	return 0;
fail:
	volatile_unlock();
	return ret ? ret : -1;
}

int spi_flash_volatile_lease_read(const struct spi_flash *flash,
	const struct spi_flash_volatile_lease *lease, u32 offset, size_t len,
	void *buf)
{
	int ret;

	if (!buf || !volatile_lease_span_valid(flash, offset, len) ||
	    volatile_lease_enter(flash, lease))
		return -1;
	ret = volatile_state.read(flash, offset, len, buf);
	return volatile_lease_leave(ret);
}

int spi_flash_volatile_lease_write(const struct spi_flash *flash,
	const struct spi_flash_volatile_lease *lease, u32 offset, size_t len,
	const void *buf)
{
	int ret;

	if (!buf || !volatile_lease_span_valid(flash, offset, len) ||
	    volatile_lease_enter(flash, lease))
		return -1;
	ret = volatile_state.write(flash, offset, len, buf);
	return volatile_lease_leave(ret);
}

int spi_flash_volatile_lease_erase(const struct spi_flash *flash,
	const struct spi_flash_volatile_lease *lease, u32 offset, size_t len)
{
	int ret;

	if (!volatile_lease_span_valid(flash, offset, len) ||
	    volatile_lease_enter(flash, lease))
		return -1;
	ret = volatile_state.erase(flash, offset, len);
	return volatile_lease_leave(ret);
}

int spi_flash_volatile_lease_sync(const struct spi_flash *flash,
	const struct spi_flash_volatile_lease *lease)
{
	u8 status;
	int ret = 0;

	if (volatile_lease_enter(flash, lease))
		return -1;
	if (volatile_state.status)
		ret = volatile_state.status(flash, &status);
	return volatile_lease_leave(ret);
}

int spi_flash_volatile_lease_end(struct spi_flash_volatile_lease *lease)
{
	const struct spi_flash *flash;
	bool valid;
	int ret = 0;

	if (!lease || !volatile_lock())
		return -1;
	if (lease != volatile_state.owner ||
	    volatile_state.lease_state == VOLATILE_LEASE_IDLE ||
	    volatile_state.lease_state == VOLATILE_LEASE_BUSY ||
	    volatile_state.boundary) {
		volatile_unlock();
		return -1;
	}
	flash = volatile_state.flash;
	valid = volatile_state.lease_state == VOLATILE_LEASE_ACTIVE &&
		volatile_lease_handle_valid(lease) &&
		volatile_lease_callbacks_valid();
	if (CONFIG(SPI_FLASH_HAS_VOLATILE_GROUP)) {
		volatile_state.boundary = 1;
		volatile_unlock();
		ret = chipset_volatile_group_end(flash);
		volatile_lock_wait();
		volatile_state.boundary = 0;
	}
	if (!volatile_lease_handle_valid(lease) ||
	    !volatile_lease_callbacks_valid())
		valid = false;
	memset(lease, 0, sizeof(*lease));
	volatile_state.cookie = 0;
	volatile_state.flash = NULL;
	volatile_state.ops = NULL;
	volatile_state.owner = NULL;
	volatile_state.read = NULL;
	volatile_state.write = NULL;
	volatile_state.erase = NULL;
	volatile_state.status = NULL;
	volatile_state.lease_state = VOLATILE_LEASE_IDLE;
	volatile_unlock();
	return valid && !ret ? 0 : -1;
}

void lb_spi_flash(struct lb_header *header)
{
	struct lb_spi_flash *flash;
	const struct spi_flash *spi_flash_dev;

	if (!CONFIG(BOOT_DEVICE_SPI_FLASH))
		return;

	flash = (struct lb_spi_flash *)lb_new_record(header);
	memset(flash, 0, sizeof(*flash));

	flash->tag = LB_TAG_SPI_FLASH;
	flash->size = sizeof(*flash);

	spi_flash_dev = boot_device_spi_flash();

	if (spi_flash_dev) {
		flash->flash_size = spi_flash_dev->size;
		flash->sector_size = spi_flash_dev->sector_size;
		flash->erase_cmd = spi_flash_dev->erase_cmd;
	} else {
		flash->flash_size = CONFIG_ROM_SIZE;
		/* Default 64k erase command should work on most flash.
		 * Uniform 4k erase only works on certain devices. */
		flash->sector_size = 64 * KiB;
		flash->erase_cmd = CMD_BLOCK_ERASE;
	}

	if (CONFIG(BOOT_DEVICE_MEMORY_MAPPED)) {
		struct flash_mmap_window *table = (struct flash_mmap_window *)(flash + 1);
		flash->mmap_count = spi_flash_get_mmap_windows(table);
		flash->size += flash->mmap_count * sizeof(*table);
	}

	/* Pass 4-byte address mode information to payload */
	if (CONFIG(SPI_FLASH_FORCE_4_BYTE_ADDR_MODE))
		flash->flags |= LB_SPI_FLASH_FLAG_IN_4BYTE_ADDR_MODE;
}

int spi_flash_ctrlr_protect_region(const struct spi_flash *flash,
				   const struct region *region,
				   const enum ctrlr_prot_type type)
{
	const struct spi_ctrlr *ctrlr;
	struct region flash_region;

	if (!flash)
		return -1;

	flash_region = region_create(0, flash->size);

	if (!region_is_subregion(&flash_region, region))
		return -1;

	ctrlr = flash->spi.ctrlr;

	if (!ctrlr)
		return -1;

	if (ctrlr->flash_protect)
		return ctrlr->flash_protect(flash, region, type);

	return -1;
}

int spi_flash_vector_helper(const struct spi_slave *slave,
	struct spi_op vectors[], size_t count,
	int (*func)(const struct spi_slave *slave, const void *dout,
		    size_t bytesout, void *din, size_t bytesin))
{
	int ret;
	void *din;
	size_t bytes_in;

	if (count < 1 || count > 2)
		return -1;

	/* SPI flash commands always have a command first... */
	if (!vectors[0].dout || !vectors[0].bytesout)
		return -1;
	/* And not read any data during the command. */
	if (vectors[0].din || vectors[0].bytesin)
		return -1;

	if (count == 2) {
		/* If response bytes requested ensure the buffer is valid. */
		if (vectors[1].bytesin && !vectors[1].din)
			return -1;
		/* No sends can accompany a receive. */
		if (vectors[1].dout || vectors[1].bytesout)
			return -1;
		din = vectors[1].din;
		bytes_in = vectors[1].bytesin;
	} else {
		din = NULL;
		bytes_in = 0;
	}

	ret = func(slave, vectors[0].dout, vectors[0].bytesout, din, bytes_in);

	if (ret) {
		vectors[0].status = SPI_OP_FAILURE;
		if (count == 2)
			vectors[1].status = SPI_OP_FAILURE;
	} else {
		vectors[0].status = SPI_OP_SUCCESS;
		if (count == 2)
			vectors[1].status = SPI_OP_SUCCESS;
	}

	return ret;
}

const struct spi_flash_ops_descriptor spi_flash_pp_0x20_sector_desc = {
	.erase_cmd = 0x20, /* Sector Erase */
	.status_cmd = 0x05, /* Read Status */
	.pp_cmd = 0x02, /* Page Program */
	.wren_cmd = 0x06, /* Write Enable */
	.ops = {
		.read = spi_flash_cmd_read,
		.write = spi_flash_cmd_write_page_program,
		.erase = spi_flash_cmd_erase,
		.status = spi_flash_cmd_status,
	},
};

const struct spi_flash_ops_descriptor spi_flash_pp_0xd8_sector_desc = {
	.erase_cmd = 0xd8, /* Sector Erase */
	.status_cmd = 0x05, /* Read Status */
	.pp_cmd = 0x02, /* Page Program */
	.wren_cmd = 0x06, /* Write Enable */
	.ops = {
		.read = spi_flash_cmd_read,
		.write = spi_flash_cmd_write_page_program,
		.erase = spi_flash_cmd_erase,
		.status = spi_flash_cmd_status,
	},
};
