/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/region.h>
#include <endian.h>
#include <fmap.h>
#include <fmap_config.h>
#include <security/vboot/vbnv.h>
#include <string.h>
#include <vb2_api.h>
#include <2nvstorage.h>

#define BANK_SIZE (FMAP_SECTION_RW_NVRAM_SIZE / 2)

struct vbnv_record {
	uint32_t sequence;
	uint32_t inverse;
	uint8_t data[VB2_NVDATA_SIZE];
	uint32_t reserved;
	uint32_t committed;
};

_Static_assert(sizeof(struct vbnv_record) == 32, "Unexpected VBNV record size");
_Static_assert(FMAP_SECTION_RW_NVRAM_SIZE >= 0x2000 && BANK_SIZE % 0x1000 == 0,
	       "Redundant VBNV needs two erase-aligned banks");

static struct {
	bool initialized;
	struct region_device device;
	uint32_t sequence;
	size_t bank;
	size_t next[2];
	uint8_t cache[VB2_NVDATA_SIZE];
} vbnv_flash;

static bool valid_record(const struct vbnv_record *record)
{
	struct vb2_context ctx = { 0 };
	uint32_t sequence = le32toh(record->sequence);

	if (record->committed || !sequence || sequence == UINT32_MAX ||
	    le32toh(record->inverse) != ~sequence)
		return false;
	memcpy(ctx.nvdata, record->data, sizeof(record->data));
	return vb2_nv_check_crc(&ctx) == VB2_SUCCESS;
}

static int init_vbnv(void)
{
	struct vbnv_record record, empty;

	memset(&vbnv_flash, 0, sizeof(vbnv_flash));
	memset(vbnv_flash.cache, 0xff, sizeof(vbnv_flash.cache));
	memset(&empty, 0xff, sizeof(empty));
	if (fmap_locate_area_as_rdev_rw("RW_NVRAM", &vbnv_flash.device) ||
	    region_device_sz(&vbnv_flash.device) != FMAP_SECTION_RW_NVRAM_SIZE)
		return -1;

	for (size_t bank = 0; bank < 2; bank++) {
		for (size_t offset = 0; offset < BANK_SIZE; offset += sizeof(record)) {
			if (rdev_readat(&vbnv_flash.device, &record,
				       bank * BANK_SIZE + offset, sizeof(record)) != sizeof(record))
				return -1;
			if (!memcmp(&record, &empty, sizeof(record)))
				continue;
			/* Torn records are occupied; never try to program over them. */
			vbnv_flash.next[bank] = offset + sizeof(record);
			if (!valid_record(&record) || le32toh(record.sequence) <= vbnv_flash.sequence)
				continue;
			vbnv_flash.sequence = le32toh(record.sequence);
			vbnv_flash.bank = bank;
			memcpy(vbnv_flash.cache, record.data, sizeof(record.data));
		}
	}
	vbnv_flash.initialized = true;
	return 0;
}

void read_vbnv_flash(uint8_t *data)
{
	if (!vbnv_flash.initialized && init_vbnv())
		return;
	memcpy(data, vbnv_flash.cache, sizeof(vbnv_flash.cache));
}

int save_vbnv_flash(const uint8_t *data)
{
	struct vbnv_record record, readback;
	const size_t body_size = offsetof(struct vbnv_record, committed);
	size_t bank, offset;

	if (!vbnv_flash.initialized && init_vbnv())
		return -1;
	if (!memcmp(data, vbnv_flash.cache, sizeof(vbnv_flash.cache)))
		return 0;
	if (vbnv_flash.sequence == UINT32_MAX - 1)
		return -1;

	memset(&record, 0xff, sizeof(record));
	record.sequence = htole32(vbnv_flash.sequence + 1);
	record.inverse = ~record.sequence;
	memcpy(record.data, data, sizeof(record.data));
	record.committed = 0;
	if (!valid_record(&record))
		return -1;
	bank = vbnv_flash.bank;
	if (vbnv_flash.next[bank] == BANK_SIZE) {
		/* Erase only the other bank, leaving the current committed state intact. */
		bank ^= 1;
		if (rdev_eraseat(&vbnv_flash.device, bank * BANK_SIZE, BANK_SIZE) != BANK_SIZE)
			goto failed;
		vbnv_flash.next[bank] = 0;
	}
	offset = bank * BANK_SIZE + vbnv_flash.next[bank];
	if (rdev_writeat(&vbnv_flash.device, &record, offset, body_size) != body_size ||
	    rdev_readat(&vbnv_flash.device, &readback, offset, body_size) != body_size ||
	    memcmp(&record, &readback, body_size))
		goto failed;

	/* Only a read-back-verified body may receive the final commit word. */
	if (rdev_writeat(&vbnv_flash.device, &record.committed, offset + body_size,
			 sizeof(record.committed)) != sizeof(record.committed) ||
	    rdev_readat(&vbnv_flash.device, &readback, offset, sizeof(readback)) != sizeof(readback) ||
	    memcmp(&record, &readback, sizeof(record)))
		goto failed;

	vbnv_flash.sequence++;
	vbnv_flash.bank = bank;
	vbnv_flash.next[bank] += sizeof(record);
	memcpy(vbnv_flash.cache, data, sizeof(vbnv_flash.cache));
	return 0;
failed:
	vbnv_flash.initialized = false;
	return -1;
}
