/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <cpu/x86/smm.h>
#include <device/pci_ops.h>
#include <fmap.h>
#include <lib.h>
#include <payload_mm_interface.h>
#include <soc/pci_devs.h>

__weak void mainboard_payload_mm_cfr_info(struct lb_payload_mm_interface_info *info)
{
}

void lb_payload_mm(struct lb_header *header)
{
	uintptr_t payload_mm_region_base;
	size_t payload_mm_region_size;
	uintptr_t handler_base;
	size_t handler_size;

	/* Enables payload to perform MM initialisation and runtime handling */
	struct lb_payload_mm_interface_info *mm_info = (void *)lb_new_record(header);

	mm_info->tag = LB_TAG_PLD_MM_INTERFACE_INFO;
	mm_info->size = sizeof(*mm_info);

	mm_info->revision = 1;
	mm_info->bootloader_smm_is_64bit = ENV_X86_64;
	mm_info->apm_cmd = APM_CNT_PAYLOAD_MM;
	mm_info->pad = 0;
	mm_info->cfr_mailbox = 0;
	mm_info->cfr_mailbox_size = 0;
	mm_info->cfr_supported_options = 0;
	mainboard_payload_mm_cfr_info(mm_info);

	/* SMRAM ranges */
	struct lb_payload_mm_smram_region *smram_desc = (void *)lb_new_record(header);

	smram_desc->tag = LB_TAG_PAYLOAD_MM_SMRAM_REGION;
	smram_desc->size = sizeof(*smram_desc);

	payload_mm_get_reserved_region(&payload_mm_region_base, &payload_mm_region_size);
	if (payload_mm_region_size <= PLD_MM_SHARED_MEMORY_MAX_SIZE ||
	    smm_subregion(SMM_SUBREGION_HANDLER, &handler_base, &handler_size))
		die("Payload MM has invalid SMRAM regions\n");

	smram_desc->descriptor.physical_start = payload_mm_region_base + PLD_MM_SHARED_MEMORY_MAX_SIZE;
	smram_desc->descriptor.physical_size = payload_mm_region_size - PLD_MM_SHARED_MEMORY_MAX_SIZE;
	smram_desc->handler.physical_start = handler_base;
	smram_desc->handler.physical_size = handler_size;

	/* Fixed communication range. Payload informs bootloader of its entrypoint */
	struct lb_payload_mm_shared_mem *mm_shared_mem = (void *)lb_new_record(header);

	mm_shared_mem->tag = LB_TAG_PAYLOAD_MM_SHARED_MEM;
	mm_shared_mem->size = sizeof(*mm_shared_mem);

	mm_shared_mem->comm_buffer.physical_start = payload_mm_region_base;
	mm_shared_mem->comm_buffer.physical_size = PLD_MM_SHARED_MEMORY_MAX_SIZE;

	struct region store;
	if (fmap_locate_area("SMMSTORE", &store) ||
	    region_offset(&store) > CONFIG_ROM_SIZE ||
	    region_sz(&store) > CONFIG_ROM_SIZE - region_offset(&store) ||
	    region_sz(&store) % CONFIG_SMMSTORE_BLOCK_SIZE ||
	    region_sz(&store) / CONFIG_SMMSTORE_BLOCK_SIZE < 3)
		die("Payload MM variable store has invalid geometry\n");

	/* Alder Lake exposes the BIOS flash at the top of the 32-bit address space. */
	struct lb_pld_mm_spi_controller_info *spi_info = (void *)lb_new_record(header);

	spi_info->tag = LB_TAG_PLD_SPI_FLASH_INFO;
	spi_info->size = sizeof(*spi_info);

	spi_info->revision = 1;
	spi_info->flags = 0;

	spi_info->spi_address.address_space_id = PLD_EFI_ACPI_3_0_PCI_CONFIGURATION_SPACE;
	spi_info->spi_address.register_bit_width = 32;
	spi_info->spi_address.register_bit_offset = 0;
	spi_info->spi_address.reserved = 0;
	spi_info->spi_address.address = CONFIG_ECAM_MMCONF_BASE_ADDRESS + PCI_BDF(PCH_DEV_SPI);
	spi_info->spi_address.value = 0;
	spi_info->store_base = (1ULL << 32) - CONFIG_ROM_SIZE + region_offset(&store);
	spi_info->store_size = region_sz(&store);
	spi_info->block_size = CONFIG_SMMSTORE_BLOCK_SIZE;
}
