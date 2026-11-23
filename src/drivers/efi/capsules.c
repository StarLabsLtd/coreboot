/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <boot/coreboot_tables.h>
#include <bootmem.h>
#include <bootmode.h>
#include <bootstate.h>
#include <cbmem.h>
#include <console/console.h>
#include <cpu/x86/pae.h>
#include <drivers/efi/efivars.h>
#include <drivers/efi/capsules.h>
#include <memrange.h>
#include <smm_call.h>
#include <smmstore.h>
#include <string.h>
#include <stdio.h>
#include <types.h>

#include <Uefi/UefiSpec.h>
#include <Guid/GlobalVariable.h>
#include <Guid/FmpCapsule.h>
#include <IndustryStandard/WindowsUxCapsule.h>

/*
 * Overview
 *
 * SG stands for scatter-gather.  SG list consists of SG blocks that describe a
 * potentially discontinuous sequence of memory blocks while not necessarily
 * lying in continuous memory themselves.
 *
 * SG list is basically a linked list of arrays of block descriptors (SG
 * blocks).  Each of SG blocks can be:
 *  - a data block, which points to capsule's data
 *  - a continuation block, which says where other SG blocks are to be found
 *  - end-of-list block, which indicates there are no more blocks
 *
 * Each of the CapsuleUpdateData* EFI variables point to some SG list which
 * might contain one or more update capsules.  SG blocks never contain data of
 * more than one of the capsules.  Boundary between capsules in an SG list is
 * determined by parsing capsule headers and counting amount of data seen so
 * far.
 *
 * There can be multiple CapsuleUpdateData* variables (CapsuleUpdateData,
 * CapsuleUpdateData1, etc.) in which case their SG lists are processed independently.
 */

/* This should be more than enough. */
#define MAX_CAPSULES 32

/* Allow a 256 MiB capsule split into 4 KiB blocks, with a continuation per block. */
#define MAX_SG_DESCRIPTORS (2 * (256 * MiB / (4 * KiB)) + 1)

/* 4 should be enough, but 8 won't hurt. */
#define CAPSULE_ALIGNMENT 8

/*
 * A helper structure which bundles physical block address with its data.  It's
 * necessary because 32-bit code can't easily access anything beyond 4 GiB
 * boundary and this structure allows reading the data, passing it around.
 *
 * Usage:
 *  1) Set .self to physical address
 *  2) Check block's address with is_good_block()
 *  3) Use load_block() to fetch the descriptor
 */
struct block_descr {
	/* Where the data comes from. */
	uint64_t self;

	/*
	 * Data read from the self address above.  Three cases:
	 *  - len != 0 && addr != 0  =>  len bytes of capsule data at addr
	 *                               next block_descr follows this one (self + 16)
	 *  - len == 0 && addr != 0  =>  no data
	 *                               next block_descr is at addr
	 *  - len == 0 && addr == 0  =>  no data
	 *                               no next block_descr
	 */
	uint64_t len;
	uint64_t addr;
};

/* For passing data from efi_parse_capsules() to bootmem and CBMEM callbacks. */
struct memory_range {
	uint32_t base;
	uint32_t len;
};

static const EFI_GUID capsule_vendor_guid = {
	0x711C703F, 0xC285, 0x4B10, { 0xA3, 0xB0, 0x36, 0xEC, 0xBD, 0x3C, 0x8B, 0xE2 }
};
static const EFI_GUID efi_global_variable_guid = EFI_GLOBAL_VARIABLE;
static const EFI_GUID windows_ux_capsule_guid = WINDOWS_UX_CAPSULE_GUID;
static const EFI_GUID edk2_capsule_on_disk_name_guid = {
	0x98C80A4F, 0xE16B, 0x4D11, { 0x93, 0x9A, 0xAB, 0xE5, 0x61, 0x26, 0x3, 0x30 }
};
static const EFI_GUID efi_fmp_capsule_guid = EFI_FIRMWARE_MANAGEMENT_CAPSULE_ID_GUID;

/* Memory map to keep track of unused or reserved ranges. */
struct memranges memory_map;

/* Page tables required for pae_map_2M_page(). */
static char pae_page_tables[20 * KiB] __aligned(4 * KiB);

/* Where all coalesced capsules are located. */
struct memory_range coalesce_buffer;

/* Where individual coalesced capsules are located and their count. */
static struct memory_range uefi_capsules[MAX_CAPSULES];
static int uefi_capsule_count;

static bool efi_is_disk_capsules_boot(void)
{
	struct region_device rdev;
	uint64_t os_indications = 0;
	uint32_t size = sizeof(os_indications);

	if (smmstore_lookup_region(&rdev))
		return false;

	if (efi_fv_get_option(&rdev, &efi_global_variable_guid, "OsIndications",
			      &os_indications, &size) != CB_SUCCESS)
		return false;

	return size == sizeof(os_indications) &&
	       (os_indications & EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED);
}

static bool is_data_block(const struct block_descr *block)
{
	return (block->len != 0);
}

static bool is_final_block(const struct block_descr *block)
{
	return (block->len == 0 && block->addr == 0);
}

static bool pae_needs_mapping(uint64_t base, uint64_t end)
{
	const uint64_t window_base = CONFIG_ECAM_MMCONF_BASE_ADDRESS;

	return end > 4ULL * GiB || (end > window_base && base < window_base + 2 * MiB);
}

static void *map_range(uint64_t base, uint32_t len)
{
	static uint64_t last_mapping_base = UINT64_MAX;

	if (ENV_X86_64) {
		assert((base + len) <= (uint64_t)CONFIG_CPU_PT_ROM_MAP_GB * GiB);

		if ((base + len) <= (uint64_t)CONFIG_CPU_PT_ROM_MAP_GB * GiB)
			/* 64-bit code can access all of memory without any mapping. */
			return (void *)(uintptr_t)base;
		else
			/* For developers: Consider increasing CONFIG_CPU_PT_ROM_MAP_GB. */
			die("capsules: memory range map request can't be satisfied.\n");
	}

	/* Using MMCONF should be safe as long as we don't do any device
	   initialization during parsing of capsules and don't forget to call
	   paging_disable_pae() at the end. */
	_Static_assert(IS_ALIGNED(CONFIG_ECAM_MMCONF_BASE_ADDRESS, 2 * MiB));
	uintptr_t window_base = CONFIG_ECAM_MMCONF_BASE_ADDRESS;

	printk(BIOS_SPEW, "capsules: mapping %#010x bytes at %#010llx.\n", len, base);

	if (!pae_needs_mapping(base, base + len)) {
		/* Don't bother with the mapping, the whole range must be
		   already accessible without it. */
		printk(BIOS_SPEW, "capsules: no need to map anything.\n");
		return (void *)(uintptr_t)base;
	}

	uint64_t aligned_base = ALIGN_DOWN(base, 2 * MiB);
	if (base - aligned_base + len > 2 * MiB)
		die("capsules: memory range map request can't be satisfied.\n");

	/* No need to map the same data. */
	if (aligned_base != last_mapping_base) {
		printk(BIOS_SPEW, "capsules: mapping from %#010llx.\n", aligned_base);
		pae_map_2M_page(&pae_page_tables, aligned_base, (void *)window_base);
		last_mapping_base = aligned_base;
	}

	return (uint8_t *)window_base + (base - aligned_base);
}

/*
 * Alignment requirement on EFI_CAPSULE_BLOCK_DESCRIPTOR seems to be 8 bytes,
 * which means that it can be cut in half by a mapping.  Could map two 2 MiB
 * pages instead, but should be easier to simply read those 16 bytes and pass
 * them around.
 *
 * `volatile` is to guard against a hypothetical statement reordering.
 */

static void load_block(struct block_descr *block)
{
	volatile uint64_t *len = map_range(block->self, sizeof(uint64_t));
	block->len = *len;

	volatile uint64_t *addr = map_range(block->self + sizeof(uint64_t), sizeof(uint64_t));
	block->addr = *addr;
}

static EFI_CAPSULE_HEADER load_capsule_header(uint64_t addr)
{
	EFI_CAPSULE_HEADER header;

	memcpy(&header, map_range(addr, sizeof(header)), sizeof(header));
	return header;
}

static void advance_block(struct block_descr *block)
{
	if (is_final_block(block))
		die("capsules: attempt to advance beyond final SG block of UEFI capsules.\n");

	if (is_data_block(block)) {
		/* That was at least part of a capsule. */
		block->self = block->self + sizeof(EFI_CAPSULE_BLOCK_DESCRIPTOR);
	} else {
		/* End of continuous sequence of descriptors, but there are more. */
		block->self = block->addr;
	}
}

static bool is_good_capsule(const EFI_CAPSULE_HEADER *capsule)
{
	if (capsule->HeaderSize < sizeof(*capsule)) {
		printk(BIOS_ERR, "capsules: capsule header size is too small: %#010x.\n",
		       capsule->HeaderSize);
		return false;
	}
	if (capsule->CapsuleImageSize <= capsule->HeaderSize) {
		printk(BIOS_ERR, "capsules: capsule image size is too small: %#010x.\n",
		       capsule->CapsuleImageSize);
		return false;
	}
	if (!(capsule->Flags & CAPSULE_FLAGS_PERSIST_ACROSS_RESET)) {
		printk(BIOS_ERR,
		       "capsules: this capsule should not have persisted, flags: %#010x.\n",
		       capsule->Flags);
		return false;
	}

	const EFI_GUID *guid = &capsule->CapsuleGuid;
	if (memcmp(guid, &windows_ux_capsule_guid, sizeof(*guid)) == 0)
		return true;
	if (memcmp(guid, &edk2_capsule_on_disk_name_guid, sizeof(*guid)) == 0)
		return true;
	if (memcmp(guid, &efi_fmp_capsule_guid, sizeof(*guid)) == 0)
		return true;

	printk(BIOS_ERR, "capsules: unrecognized capsule GUID.\n");
	return false;
}

static bool is_in_unused_ram(uint64_t base, uint64_t len)
{
	if (!base || !len || len > UINT64_MAX - base) {
		printk(BIOS_ERR,
		       "capsules: %s() was passed an invalid range: %#010llx:%#010llx.\n",
		       __func__, base, len);
		return false;
	}

	const uint64_t end = base + len;
	if (ENV_X86_64 && end > (uint64_t)CONFIG_CPU_PT_ROM_MAP_GB * GiB)
		return false;

	const struct range_entry *r;
	memranges_each_entry(r, &memory_map) {
		if (range_entry_tag(r) != BM_MEM_RAM)
			continue;

		if (base >= range_entry_base(r) && end <= range_entry_end(r))
			return true;
	}

	return false;
}

static bool is_good_block(struct block_descr *block)
{
	if (!IS_ALIGNED(block->self, sizeof(uint64_t))) {
		printk(BIOS_ERR, "capsules: misaligned SG block at %#010llx.\n", block->self);
		return false;
	}

	if (!is_in_unused_ram(block->self, sizeof(EFI_CAPSULE_BLOCK_DESCRIPTOR))) {
		printk(BIOS_ERR, "capsules: SG block is not in unused memory.\n");
		return false;
	}

	return true;
}

static bool is_good_capsule_head(struct block_descr *block)
{
	if (!is_data_block(block)) {
		printk(BIOS_ERR, "capsules: first capsule SG block is not a data block.\n");
		return false;
	}

	if (block->len < sizeof(EFI_CAPSULE_HEADER)) {
		printk(BIOS_ERR, "capsules: first SG block of a capsule is too small.\n");
		return false;
	}

	if (!is_in_unused_ram(block->addr, block->len)) {
		printk(BIOS_ERR, "capsules: capsule header is not in unused memory.\n");
		return false;
	}

	/* A remapped header must fit in one PAE window. */
	const size_t header_size = sizeof(EFI_CAPSULE_HEADER);
	if (!ENV_X86_64 && pae_needs_mapping(block->addr, block->addr + header_size) &&
	    header_size > 2 * MiB - block->addr % (2 * MiB)) {
		printk(BIOS_ERR, "capsules: capsule header crosses a PAE window.\n");
		return false;
	}

	return true;
}

static bool is_good_capsule_block(struct block_descr *block, uint32_t size_left)
{
	if (is_final_block(block)) {
		printk(BIOS_ERR, "capsules: not enough SG blocks to cover a capsule.\n");
		return false;
	}

	if (!is_data_block(block)) {
		printk(BIOS_ERR, "capsules: capsule SG block is not a data block.\n");
		return false;
	}

	if (block->len > size_left) {
		printk(BIOS_ERR, "capsules: SG blocks reach beyond a capsule.\n");
		return false;
	}

	if (!is_in_unused_ram(block->addr, block->len)) {
		printk(BIOS_ERR, "capsules: capsule data is not in unused memory.\n");
		return false;
	}

	return true;
}

/* Validate one SG list and account for its data only if it is complete. */
static bool check_capsule_block(struct block_descr first_block, unsigned int *capsule_count,
				uint64_t *total_data_size)
{
	struct block_descr block = first_block;
	if (!is_good_block(&block)) {
		printk(BIOS_ERR, "capsules: bad capsule block start.\n");
		goto error;
	}

	load_block(&block);

	unsigned int descriptors = 1;
	unsigned int count = *capsule_count;
	uint64_t data_size = 0;
	while (!is_final_block(&block)) {
		if (!is_good_capsule_head(&block)) {
			printk(BIOS_ERR, "capsules: bad capsule header @ %#010llx.\n",
			       block.addr);
			goto error;
		}

		const EFI_CAPSULE_HEADER header = load_capsule_header(block.addr);
		if (!is_good_capsule(&header)) {
			printk(BIOS_ERR, "capsules: bad capsule header @ %#010llx.\n",
			       block.addr);
			goto error;
		}

		if (count < MAX_CAPSULES)
			data_size += ALIGN_UP((uint64_t)header.CapsuleImageSize, CAPSULE_ALIGNMENT);
		count++;

		uint32_t size_left = header.CapsuleImageSize;
		while (size_left != 0) {
			/* is_good_block() holds here whether it's the first iteration or
			   not. */

			if (!is_good_capsule_block(&block, size_left))
				goto error;

			size_left -= block.len;

			advance_block(&block);
			if (!is_good_block(&block)) {
				printk(BIOS_ERR, "capsules: capsule body has a bad block.\n");
				goto error;
			}

			if (++descriptors > MAX_SG_DESCRIPTORS)
				goto error;
			load_block(&block);
			if (!is_final_block(&block) && !is_data_block(&block)) {
				/* Advance to the next page of block descriptors. */
				advance_block(&block);
				if (!is_good_block(&block)) {
					printk(BIOS_ERR, "capsules: bad SG continuation.\n");
					goto error;
				}

				if (++descriptors > MAX_SG_DESCRIPTORS)
					goto error;
				load_block(&block);
				/* Not expecting a continuation to be followed by another
				   continuation or an end-of-list. */
				if (!is_data_block(&block)) {
					printk(BIOS_ERR,
					       "capsules: chained SG continuations.\n");
					goto error;
				}
			}
		}
	}

	/* The descriptor limit bounds the count. Only publishable sizes are summed. */
	*capsule_count = count;
	*total_data_size += data_size;

	return true;

error:
	return false;
}

/* Fills an array with pointers to capsule blocks.  Returns number of
   discovered capsule blocks or -1 on error. */
static int discover_capsule_blocks(struct region_device *rdev,
				   struct block_descr *blocks,
				   int max_blocks)
{
	int block_count = 0;
	for (int i = 0; i < max_blocks; ++i) {
		char var_name[32];
		if (i == 0)
			strcpy(var_name, "CapsuleUpdateData");
		else
			snprintf(var_name, sizeof(var_name), "CapsuleUpdateData%d", i);

		struct block_descr block;
		uint32_t size = sizeof(block.self);
		enum cb_err ret = efi_fv_get_option(rdev, &capsule_vendor_guid, var_name,
						    &block.self, &size);
		if (ret != CB_SUCCESS) {
			/* No more variables. */
			break;
		}
		if (size != sizeof(block.self)) {
			printk(BIOS_ERR, "capsules: unexpected capsule data size (%d).\n",
			       size);
			return -1;
		}

		int j;
		for (j = 0; j < block_count; ++j) {
			if (blocks[j].self == block.self)
				break;
		}
		if (j < block_count) {
			printk(BIOS_INFO, "capsules: skipping duplicated %s.\n", var_name);
			continue;
		}

		printk(BIOS_INFO, "capsules: capsule block #%d at %#010llx.\n",
		       block_count, block.self);
		blocks[block_count++] = block;
	}

	return block_count;
}

/* Marks structures and data of SG lists as BM_MEM_RESERVED so we don't step on
   them when looking for usable memory. */
static bool reserve_capsules(struct block_descr block)
{
	unsigned int descriptors = 1;

	/* This is the first block of a continuous sequence of blocks. */
	struct block_descr seq_start = {0};

	/* The code reserves sequences of blocks to avoid invoking
	   memranges_insert() on each of a bunch of adjacent 16-byte blocks. */

	load_block(&block);
	while (!is_final_block(&block)) {
		if (++descriptors > MAX_SG_DESCRIPTORS)
			return false;
		if (seq_start.self == 0)
			seq_start = block;

		if (is_data_block(&block)) {
			/* Reserve capsule data. */
			memranges_insert(&memory_map, block.addr, block.len, BM_MEM_RESERVED);
		} else {
			/* This isn't the final or a data block, so it must be the
			   last block of a continuous sequence.  Reserve the whole
			   sequence. */
			memranges_insert(&memory_map,
					 seq_start.self,
					 block.self - seq_start.self +
					 sizeof(EFI_CAPSULE_BLOCK_DESCRIPTOR),
					 BM_MEM_RESERVED);

			/* Will be set on the next iteration if there is one. */
			seq_start.self = 0;
		}
		advance_block(&block);
		load_block(&block);
	}

	if (!seq_start.self)
		seq_start = block;
	memranges_insert(&memory_map, seq_start.self,
			 block.self - seq_start.self + sizeof(EFI_CAPSULE_BLOCK_DESCRIPTOR),
			 BM_MEM_RESERVED);
	return true;
}

/*
 * Find a directly accessible buffer below 4 GiB for coalesced capsules.
 *
 * Keeping it simple and allocating a single buffer.  However, there is
 * no requirement to put all the capsules together, only that each of
 * them is continuous in memory.  So if this is bad for some reason,
 * can allocate a separate block for each.
 *
 * Returns buffer that starts at 0 on error.
 */
static struct memory_range pick_buffer(uint64_t total_data_size)
{
	struct memory_range buffer = {0};
	uint64_t limit = 4ULL * GiB;
	if (ENV_X86_64)
		limit = MIN(limit, (uint64_t)CONFIG_CPU_PT_ROM_MAP_GB * GiB);

	/* 4 * KiB is the alignment set by memranges_init(). */
	total_data_size = ALIGN_UP(total_data_size, 4 * KiB);

	const struct range_entry *r;
	memranges_each_entry(r, &memory_map) {
		if (range_entry_tag(r) != BM_MEM_RAM)
			continue;

		resource_t base = range_entry_base(r);
		if (base >= limit)
			break;

		resource_t size = MIN(range_entry_end(r), limit) - base;

		if (size >= total_data_size) {
			/*
			 * To not create troubles for payloads prefer higher addresses:
			 *  - use the top part of a suitable range
			 *  - exit the loop only after hitting 4 GiB boundary or end of the list
			 */
			buffer.base = base + size - total_data_size;
			buffer.len = total_data_size;
		}
	}

	return buffer;
}

/* Coalesce from SG lists that remain unchanged after validation and reservation. */
static bool coalesce_capsules(struct block_descr block, uint32_t *used)
{
	unsigned int descriptors = 0;
	uint32_t offset = *used;
	uint32_t aligned_size = 0;
	uint32_t capsule_size = 0;
	uint32_t size_left = 0;

	load_block(&block);
	for (; !is_final_block(&block); advance_block(&block), load_block(&block)) {
		if (++descriptors >= MAX_SG_DESCRIPTORS)
			return false;
		/* Advance over a continuation. */
		if (!is_data_block(&block))
			continue;

		/* This must be the first block of a capsule. */
		if (size_left == 0) {
			const EFI_CAPSULE_HEADER header = load_capsule_header(block.addr);
			capsule_size = header.CapsuleImageSize;
			const uint64_t size = ALIGN_UP((uint64_t)capsule_size, CAPSULE_ALIGNMENT);
			if (!capsule_size || size > coalesce_buffer.len - *used)
				return false;
			aligned_size = size;
			size_left = capsule_size;
		}

		if (block.len > size_left)
			return false;

		uint64_t addr = block.addr;
		uint64_t data_left = block.len;
		while (data_left != 0) {
			uint64_t piece_len = MIN(data_left, 2 * MiB - (addr % (2 * MiB)));
			void *data = map_range(addr, piece_len);
			void *target = (void *)(uintptr_t)(coalesce_buffer.base + offset);

			memcpy(target, data, piece_len);

			offset += piece_len;
			addr += piece_len;
			data_left -= piece_len;
		}

		size_left -= block.len;

		/* This must be the last block of a capsule, record it. */
		if (size_left == 0) {
			uefi_capsules[uefi_capsule_count].base = coalesce_buffer.base + *used;
			uefi_capsules[uefi_capsule_count].len = capsule_size;
			*used += aligned_size;
			offset = *used;
			if (++uefi_capsule_count == MAX_CAPSULES)
				return true;
		}
	}

	return size_left == 0;
}

void efi_parse_capsules(void)
{
	/* EDK2 starts with 20 items and then grows the list, but it's unlikely
	   to be necessary in practice. */
	enum { MAX_CAPSULE_BLOCKS = MAX_CAPSULES };

	static bool once;
	if (!once) {
		once = true;
	} else {
		printk(BIOS_DEBUG, "capsules: already parsed, skipping.\n");
		return;
	}

	if (CONFIG(DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT) && efi_is_disk_capsules_boot()) {
		set_boot_mode(LB_BOOT_MODE_FLASH_UPDATE);
		return;
	}

	struct region_device rdev;
	if (smmstore_lookup_region(&rdev)) {
		printk(BIOS_INFO, "capsules: no SMMSTORE region, no update capsules.\n");
		return;
	}

	void *cbmem_current;
	size_t cbmem_size;
	if (cbmem_get_region(&cbmem_current, &cbmem_size)) {
		printk(BIOS_ERR, "capsules: cannot locate CBMEM.\n");
		return;
	}
	const uint64_t cbmem_base = (uintptr_t)cbmem_current;
	if (!cbmem_size || cbmem_size > UINT64_MAX - cbmem_base ||
	    cbmem_base + cbmem_size > ALIGN_DOWN(UINT64_MAX, 4 * KiB)) {
		printk(BIOS_ERR, "capsules: invalid CBMEM range.\n");
		return;
	}

	memranges_init(&memory_map, IORESOURCE_MEM | IORESOURCE_FIXED | IORESOURCE_STORED |
		       IORESOURCE_ASSIGNED | IORESOURCE_CACHEABLE, IORESOURCE_MEM |
		       IORESOURCE_FIXED | IORESOURCE_STORED | IORESOURCE_ASSIGNED |
		       IORESOURCE_CACHEABLE, BM_MEM_RAM);
	memranges_add_resources(&memory_map, IORESOURCE_RESERVE, IORESOURCE_RESERVE,
				BM_MEM_RESERVED);
	memranges_add_resources(&memory_map, IORESOURCE_SOFT_RESERVE, IORESOURCE_SOFT_RESERVE,
				BM_MEM_SOFT_RESERVED);
	memranges_create_hole(&memory_map, cbmem_base, cbmem_size);

	if (ENV_X86_32)
		init_pae_pagetables(&pae_page_tables);

	/* Blocks are collected here when traversing CapsuleUpdateData*
	   variables, duplicates are skipped. */
	struct block_descr blocks[MAX_CAPSULE_BLOCKS];
	int block_count = discover_capsule_blocks(&rdev, blocks, ARRAY_SIZE(blocks));
	if (block_count <= 0) {
		if (block_count == 0)
			printk(BIOS_INFO, "capsules: no UEFI capsules were discovered.\n");
		goto exit;
	}

	printk(BIOS_INFO, "capsules: processing %d capsule block(s).\n", block_count);

	/* Broken capsules are ignored, ignore those which didn't fit as well. */
	if (block_count == ARRAY_SIZE(blocks)) {
		printk(BIOS_WARNING,
		       "capsules: hit limit on capsule blocks, some might be ignored.\n");
	}

	uint64_t total_data_size = 0;
	unsigned int capsule_count = 0;
	int valid_count = 0;
	for (int i = 0; i < block_count; i++) {
		if (check_capsule_block(blocks[i], &capsule_count, &total_data_size))
			blocks[valid_count++] = blocks[i];
		else
			printk(BIOS_WARNING, "capsules: skipping damaged capsule block #%d.\n", i);
	}
	if (!total_data_size || total_data_size > ALIGN_DOWN(UINT32_MAX, 4 * KiB)) {
		printk(BIOS_ERR, "capsules: no usable capsule data to process.\n");
		goto exit;
	}
	if (capsule_count > MAX_CAPSULES)
		printk(BIOS_WARNING, "capsules: ignoring all capsules after #%d.\n", MAX_CAPSULES);

	/* Keep source descriptors and data out of the destination buffer. */
	for (int i = 0; i < valid_count; i++)
		if (!reserve_capsules(blocks[i]))
			goto exit;

	printk(BIOS_DEBUG, "capsules: reserved capsule blocks.\n");

	/* Leave at least 4 MiB below CBMEM for further allocations when possible. */
	const uint64_t cbmem_future_base = ALIGN_DOWN(cbmem_base - MIN(cbmem_base, 4 * MiB), MiB);
	memranges_create_hole(&memory_map, cbmem_future_base, cbmem_base - cbmem_future_base);

	coalesce_buffer = pick_buffer(total_data_size);
	if (coalesce_buffer.base == 0) {
		printk(BIOS_ERR,
		       "capsules: failed to find a buffer (%#llx bytes) for coalesced UEFI capsules.\n",
		       total_data_size);
	} else {
		printk(BIOS_DEBUG, "capsules: coalescing capsules data @ %#010x.\n",
		       coalesce_buffer.base);
		uint32_t used = 0;
		for (int i = 0; i < valid_count && uefi_capsule_count < MAX_CAPSULES; i++) {
			if (!coalesce_capsules(blocks[i], &used)) {
				uefi_capsule_count = 0;
				coalesce_buffer = (struct memory_range){0};
				goto exit;
			}
		}
		printk(BIOS_INFO, "capsules: found %d capsule(s).\n", uefi_capsule_count);
	}
	if (uefi_capsule_count > 0)
		set_boot_mode(LB_BOOT_MODE_FLASH_UPDATE);

exit:
	if (ENV_X86_32)
		paging_disable_pae();
	memranges_teardown(&memory_map);
}

void lb_efi_capsules(struct lb_header *header)
{
	int i;
	for (i = 0; i < uefi_capsule_count; ++i) {
		struct lb_range *capsule = (void *)lb_new_record(header);

		printk(BIOS_INFO, "capsules: publishing a capsule @ %#010x.\n",
		       uefi_capsules[i].base);

		capsule->tag = LB_TAG_CAPSULE;
		capsule->size = sizeof(*capsule);
		capsule->range_start = uefi_capsules[i].base;
		capsule->range_size = uefi_capsules[i].len;
	}
}

void efi_add_capsules_to_bootmem(void)
{
	if (coalesce_buffer.len != 0) {
		printk(BIOS_INFO, "capsules: reserving capsules data @ %#010x.\n",
		       coalesce_buffer.base);
		bootmem_add_range(coalesce_buffer.base, coalesce_buffer.len, BM_MEM_RESERVED);
	}
}

/*
 * The code from this unit is typically executed by clear_memory() which is run
 * after DEV_INIT.  However, clear_memory() might not be compiled in in which
 * case we still want to process capsules.
 *
 * State machine doesn't enforce any particular ordering for callbacks and
 * running before DEV_INIT is too early due to MTTRs not being initialized.
 * Hence invoking code is in two different places that should be mutually
 * exclusive (can't set a "done" flag due to unknown ordering).
 */
#if !CONFIG(PLATFORM_HAS_DRAM_CLEAR)

static void parse_capsules(void *unused)
{
	if (!acpi_is_wakeup_s3())
		efi_parse_capsules();
}

BOOT_STATE_INIT_ENTRY(BS_DEV_INIT, BS_ON_EXIT, parse_capsules, NULL);

#endif

static void enable_capsule_smi(void *unused)
{
	uint32_t ret;
	const bool supported = get_boot_mode() == LB_BOOT_MODE_FLASH_UPDATE;
	ret = call_smm(APM_CNT_SMMSTORE, SMMSTORE_CMD_USE_FULL_FLASH,
		       (void *)(uintptr_t)supported);

	if (ret != SMMSTORE_RET_SUCCESS)
		printk(BIOS_ERR, "Failed to configure capsule update SMI handler\n");
	else
		printk(BIOS_INFO, "%sabled capsule update SMI handler\n",
		       supported ? "En" : "Dis");
}

BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, enable_capsule_smi, NULL);

static void disable_capsule_smi_on_resume(void *unused)
{
	if (call_smm(APM_CNT_SMMSTORE, SMMSTORE_CMD_USE_FULL_FLASH, NULL) !=
	    SMMSTORE_RET_SUCCESS)
		die("Failed to disable capsule update SMI handler on resume\n");
}

BOOT_STATE_INIT_ENTRY(BS_OS_RESUME, BS_ON_ENTRY, disable_capsule_smi_on_resume, NULL);
