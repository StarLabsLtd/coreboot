/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>
#include <boot/capsule_platform_facts.h>
#include <boot/coreboot_tables.h>
#include <boot_device.h>
#include <spi_flash.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Capsule platform facts must only be collected in SMM"
#endif

static uint32_t control;
static struct capsule_platform_facts snapshot;

static bool ranges_overlap(uint64_t left, uint64_t left_size,
	uint64_t right, uint64_t right_size)
{
	if (!left_size || !right_size || left > UINT64_MAX - left_size ||
	    right > UINT64_MAX - right_size)
		return true;
	return left < right + right_size && right < left + left_size;
}

enum cb_err capsule_platform_facts_collect(struct capsule_platform_facts *facts)
{
	const struct region_device *boot;
	const struct spi_flash *flash;
	uint32_t expected = 0;
	uint32_t flash_size;
	uint32_t page_size;
	uint32_t sector_size;
	size_t boot_size;

	if (!__atomic_compare_exchange_n(&control, &expected, 1, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (!facts)
		return CB_ERR;
	memset(&snapshot, 0, sizeof(snapshot));
	boot_device_init();
	boot = boot_device_ro();
	flash = boot_device_spi_flash();
	if (!boot || !flash)
		return CB_ERR;
	boot_size = region_device_sz(boot);
	flash_size = flash->size;
	page_size = flash->page_size;
	sector_size = flash->sector_size;
	if (ranges_overlap((uintptr_t)facts, sizeof(*facts), (uintptr_t)boot,
		sizeof(*boot)) ||
	    ranges_overlap((uintptr_t)facts, sizeof(*facts), (uintptr_t)flash,
		sizeof(*flash)) ||
	    !flash_size || !page_size || !sector_size || flash_size != boot_size ||
	    flash_size % sector_size || sector_size % page_size ||
	    fmap_read_inventory(&snapshot.fmap) ||
	    snapshot.fmap.size != flash_size ||
	    !capsule_broker_buffers_get(&snapshot.buffers) ||
	    snapshot.buffers.communication_size != CAPSULE_BROKER_TRANSPORT_SIZE ||
	    snapshot.buffers.communication_reserved_size <
		snapshot.buffers.communication_size ||
	    snapshot.buffers.staging_size < flash_size ||
	    capsule_broker_scratch_acquire(sector_size,
		&snapshot.scratch) != CB_SUCCESS ||
	    efi_fw_info_get(&snapshot.firmware) != CB_SUCCESS ||
	    snapshot.firmware.fw_size != flash_size)
		return CB_ERR;
	snapshot.revision = CAPSULE_PLATFORM_FACTS_REVISION;
	snapshot.size = sizeof(snapshot);
	snapshot.boot_media_size = flash_size;
	snapshot.block_size = page_size;
	snapshot.erase_size = sector_size;
	if (snapshot.buffers.communication_base > UINTPTR_MAX ||
	    snapshot.buffers.staging_base > UINTPTR_MAX ||
	    ranges_overlap(snapshot.buffers.communication_base,
		snapshot.buffers.communication_reserved_size,
		snapshot.buffers.staging_base, snapshot.buffers.staging_size) ||
	    snapshot.scratch.erase_size != sector_size ||
	    snapshot.scratch.write_base > UINTPTR_MAX ||
	    snapshot.scratch.read_base > UINTPTR_MAX ||
	    ranges_overlap((uintptr_t)facts, sizeof(*facts),
		snapshot.buffers.communication_base,
		snapshot.buffers.communication_reserved_size) ||
	    ranges_overlap((uintptr_t)facts, sizeof(*facts),
		snapshot.buffers.staging_base, snapshot.buffers.staging_size) ||
	    ranges_overlap((uintptr_t)facts, sizeof(*facts),
		snapshot.scratch.write_base, snapshot.scratch.erase_size) ||
	    ranges_overlap((uintptr_t)facts, sizeof(*facts),
		snapshot.scratch.read_base, snapshot.scratch.erase_size) ||
	    ranges_overlap(snapshot.scratch.write_base,
		snapshot.scratch.erase_size, snapshot.scratch.read_base,
		snapshot.scratch.erase_size) ||
	    ranges_overlap(snapshot.scratch.write_base,
		snapshot.scratch.erase_size, snapshot.buffers.communication_base,
		snapshot.buffers.communication_reserved_size) ||
	    ranges_overlap(snapshot.scratch.write_base,
		snapshot.scratch.erase_size, snapshot.buffers.staging_base,
		snapshot.buffers.staging_size) ||
	    ranges_overlap(snapshot.scratch.read_base, snapshot.scratch.erase_size,
		snapshot.buffers.communication_base,
		snapshot.buffers.communication_reserved_size) ||
	    ranges_overlap(snapshot.scratch.read_base, snapshot.scratch.erase_size,
		snapshot.buffers.staging_base, snapshot.buffers.staging_size))
		return CB_ERR;
	if (boot_device_ro() != boot || boot_device_spi_flash() != flash ||
	    region_device_sz(boot) != boot_size || flash->size != flash_size ||
	    flash->page_size != page_size || flash->sector_size != sector_size)
		return CB_ERR;
	*facts = snapshot;
	return CB_SUCCESS;
}
