/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <commonlib/region.h>
#include <cpu/x86/smm.h>
#include <fmap.h>
#include <fmap_config.h>
#include <intelblocks/fast_spi.h>
#include <smmstore.h>
#include <string.h>
#include <tests/test.h>

static struct region_device flash;
static uint8_t bytes[FMAP_SECTION_FLASH_SIZE];
static uint8_t communication_buffer[SMM_BLOCK_SIZE];
static const uint8_t name[34] = { 0, 0x0b, 0x53, 0x52, 0x4b };
static bool s3_resume, eiss, short_write, short_read;
static size_t store_offset, store_size;
static unsigned int writes, erases;

static ssize_t flash_readat(const struct region_device *rdev, void *buffer,
			    size_t offset, size_t size)
{
	if (short_read)
		size--;
	memcpy(buffer, bytes + offset, size);
	return size;
}

static ssize_t flash_writeat(const struct region_device *rdev, const void *buffer,
			     size_t offset, size_t size)
{
	const uint8_t *src = buffer;
	writes++;
	if (short_write)
		size--;
	for (size_t i = 0; i < size; i++)
		bytes[offset + i] &= src[i];
	return size;
}

static ssize_t flash_eraseat(const struct region_device *rdev, size_t offset, size_t size)
{
	erases++;
	memset(bytes + offset, 0xff, size);
	return size;
}

static const struct region_device_ops flash_ops = {
	.readat = flash_readat,
	.writeat = flash_writeat,
	.eraseat = flash_eraseat,
};

int fmap_locate_area(const char *region_name, struct region *region)
{
	assert_string_equal(region_name, "SMMSTORE");
	*region = region_create(store_offset, store_size);
	return 0;
}

const struct region_device *boot_device_rw(void)
{
	return &flash;
}

int boot_device_ro_subregion(const struct region *sub, struct region_device *subrd)
{
	return rdev_chain(subrd, &flash, region_offset(sub), region_sz(sub));
}

int boot_device_rw_subregion(const struct region *sub, struct region_device *subrd)
{
	return rdev_chain(subrd, &flash, region_offset(sub), region_sz(sub));
}

bool smm_region_overlaps_handler(const struct region *region)
{
	return false;
}

bool smm_is_s3_resume(void)
{
	return s3_resume;
}

bool fast_spi_eiss_status(void)
{
	return eiss;
}

void smm_get_smmstore_com_buffer(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)communication_buffer;
	*size = sizeof(communication_buffer);
}

static uint32_t pin_command(uint8_t command)
{
	return smmstore_exec(command, communication_buffer);
}

static int setup(void **state)
{
	region_device_init(&flash, &flash_ops, 0, sizeof(bytes));
	memset(bytes, 0xff, sizeof(bytes));
	memset(communication_buffer, 0, sizeof(communication_buffer));
	s3_resume = short_write = short_read = false;
	store_offset = FMAP_SECTION_SMMSTORE_START;
	store_size = FMAP_SECTION_SMMSTORE_SIZE;
	eiss = true;
	writes = erases = 0;
	return 0;
}

static void raw_ranges(void **state)
{
	struct smmstore_params_raw_write write = { .bufsize = 1 };
	struct smmstore_params_raw_clear clear;

	assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH, (void *)1), 0);
	for (unsigned int mode = 0; mode <= SMMSTORE_CMD_USE_FULL_FLASH;
	     mode += SMMSTORE_CMD_USE_FULL_FLASH) {
		for (unsigned int block = 0; block < 4; block++) {
			bool protected = block == 1 || block == 2;
			write.block_id = clear.block_id = block;
			unsigned int old_writes = writes, old_erases = erases;
			assert_int_equal(smmstore_exec(mode | SMMSTORE_CMD_RAW_WRITE, &write),
				protected ? SMMSTORE_RET_FAILURE : SMMSTORE_RET_SUCCESS);
			assert_int_equal(smmstore_exec(mode | SMMSTORE_CMD_RAW_CLEAR, &clear),
				protected ? SMMSTORE_RET_FAILURE : SMMSTORE_RET_SUCCESS);
			assert_int_equal(writes - old_writes, !protected);
			assert_int_equal(erases - old_erases, !protected);
		}
	}
	/* A partial write cannot cross a logical block into a protected block. */
	write.block_id = 0;
	write.bufoffset = SMM_BLOCK_SIZE - 1;
	write.bufsize = 2;
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_RAW_WRITE, &write), SMMSTORE_RET_FAILURE);
}

static void raw_physical_offset(void **state)
{
	struct smmstore_params_raw_write write = { .bufsize = 1 };
	struct smmstore_params_raw_clear clear = { .block_id = 0 };

	store_offset = FMAP_SECTION_SRK_PIN_V1_START;
	store_size = FMAP_SECTION_SRK_PIN_V1_SIZE;
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_RAW_WRITE, &write), SMMSTORE_RET_FAILURE);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_RAW_CLEAR, &clear), SMMSTORE_RET_FAILURE);
	assert_int_equal(writes + erases, 0);
}

static void short_io(void **state)
{
	struct smmstore_params_raw_write write = { .bufsize = 2 };
	struct smmstore_params_raw_read read = { .bufsize = 2 };

	short_write = true;
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_RAW_WRITE, &write), SMMSTORE_RET_FAILURE);
	short_read = true;
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_RAW_READ, &read), SMMSTORE_RET_FAILURE);
}

static void provisioning(void **state)
{
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_READ),
		CONFIG(SMMSTORE_SRK_PROVISION) ? SMMSTORE_RET_NOT_FOUND : SMMSTORE_RET_FAILURE);
	memcpy(communication_buffer, name, sizeof(name));
	eiss = false;
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_CREATE), SMMSTORE_RET_FAILURE);
	eiss = true;
	s3_resume = true;
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_CREATE), SMMSTORE_RET_DENIED);
	s3_resume = false;
	if (!CONFIG(SMMSTORE_SRK_PROVISION)) {
		assert_int_equal(pin_command(SMMSTORE_CMD_SRK_CREATE), SMMSTORE_RET_DENIED);
		assert_int_equal(writes, 0);
		return;
	}
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_CREATE), SMMSTORE_RET_SUCCESS);
	assert_int_equal(writes, 2);
	assert_int_equal(erases, 0);
	assert_memory_equal(bytes + FMAP_SECTION_SRK_PIN_V1_START, "SRK1", 4);
	assert_memory_equal(bytes + FMAP_SECTION_SRK_PIN_V1_START + 4, name, sizeof(name));
	assert_int_equal(bytes[FMAP_SECTION_SRK_PIN_V1_START + 38], 0);
	assert_int_equal(bytes[FMAP_SECTION_SRK_PIN_V1_START + 39], 0);
	assert_int_equal(bytes[FMAP_SECTION_SRK_PIN_V1_START + 40], 0xff);
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_READ), SMMSTORE_RET_SUCCESS);
	struct smmstore_srk_info *info = (void *)communication_buffer;
	assert_int_equal(info->revision, 1);
	assert_int_equal(info->offset, FMAP_SECTION_SRK_PIN_V1_START);
	assert_memory_equal(info->name, name, sizeof(name));
	memcpy(communication_buffer, name, sizeof(name));
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_CREATE), SMMSTORE_RET_SUCCESS);
	communication_buffer[2] ^= 1;
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_CREATE), SMMSTORE_RET_DENIED);
	assert_int_equal(writes, 2);
}

#if CONFIG(SMMSTORE_SRK_PROVISION)
static void torn_provisioning(void **state)
{
	memcpy(communication_buffer, name, sizeof(name));
	short_write = true;
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_CREATE), SMMSTORE_RET_FAILURE);
	short_write = false;
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_READ), SMMSTORE_RET_FAILURE);
	memcpy(communication_buffer, name, sizeof(name));
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_CREATE), SMMSTORE_RET_FAILURE);
	assert_int_equal(writes, 1);
	assert_int_equal(erases, 0);

	/* A partially programmed commit marker cannot publish or replace a pin. */
	memset(bytes, 0xff, sizeof(bytes));
	memcpy(bytes + FMAP_SECTION_SRK_PIN_V1_START, "SRK1", 4);
	memcpy(bytes + FMAP_SECTION_SRK_PIN_V1_START + 4, name, sizeof(name));
	bytes[FMAP_SECTION_SRK_PIN_V1_START + 38] = 0;
	writes = 0;
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_READ), SMMSTORE_RET_FAILURE);
	memcpy(communication_buffer, name, sizeof(name));
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_CREATE), SMMSTORE_RET_FAILURE);
	assert_int_equal(writes + erases, 0);
}
#endif

/* Last: closure intentionally survives subsequent calls and cannot be reset. */
static void close_boundary(void **state)
{
	struct smmstore_params_raw_clear clear = { .block_id = 0 };
	s3_resume = true;
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH, (void *)1),
		SMMSTORE_RET_UNSUPPORTED);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH | SMMSTORE_CMD_RAW_CLEAR,
		&clear), SMMSTORE_RET_UNSUPPORTED);
	s3_resume = false;
	assert_int_equal(pin_command(SMMSTORE_CMD_SECURITY_CLOSE), SMMSTORE_RET_SUCCESS);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH, (void *)1),
		SMMSTORE_RET_UNSUPPORTED);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH | SMMSTORE_CMD_RAW_CLEAR,
		&clear), SMMSTORE_RET_UNSUPPORTED);
	memcpy(communication_buffer, name, sizeof(name));
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_CREATE), SMMSTORE_RET_DENIED);
	assert_int_equal(pin_command(SMMSTORE_CMD_SRK_READ), SMMSTORE_RET_FAILURE);
	assert_int_equal(writes + erases, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(raw_ranges, setup),
		cmocka_unit_test_setup(raw_physical_offset, setup),
		cmocka_unit_test_setup(short_io, setup),
		cmocka_unit_test_setup(provisioning, setup),
#if CONFIG(SMMSTORE_SRK_PROVISION)
		cmocka_unit_test_setup(torn_provisioning, setup),
#endif
		cmocka_unit_test_setup(close_boundary, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
