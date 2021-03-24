/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/hlt.h>
#include <console/console.h>
#include <string.h>
#include <commonlib/coreboot_tables.h>
#include <cbmem.h>
#include <hob.h>
#include <Guid/GraphicsInfoHob.h>

#define SERIAL_INFO_GUID \
	{0xaa7e190d, 0xbe21, 0x4409, {0x8e, 0x67, 0xa2, 0xcd, 0xf, 0x61, 0xe1, 0x70} }

#define EFI_ACPI_TABLE_GUID \
	{0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81} }

#define SMBIOS3_TABLE_GUID \
	{0xf2fd1544, 0x9794, 0x4a2c, {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94} }

#define MEM_RANGE_COUNT(_rec) \
	(((_rec)->size - sizeof(*(_rec))) / sizeof((_rec)->map[0]))

#define MEM_RANGE_PTR(_rec, _idx) \
	(void *)(((UINT8 *) (_rec)) + sizeof(*(_rec)) \
	+ (sizeof((_rec)->map[0]) * (_idx)))

typedef struct {
	UINT64	TableAddress;
} TABLE_HOB;

static VOID *FindCbTag (struct lb_header *Header, UINT32 Tag)
{
	struct	lb_record	*Record;
	UINT8			*TmpPtr;
	UINT8			*TagPtr;
	UINTN			Idx;

	TagPtr = NULL;
	TmpPtr = (UINT8 *)Header + Header->header_bytes;
	for (Idx = 0; Idx < Header->table_entries; Idx++) {
		Record = (struct lb_record *)TmpPtr;
		if (Record->tag == Tag) {
			TagPtr = TmpPtr;
			break;
		}
		TmpPtr += Record->size;
	}
	return TagPtr;
}

static EFI_STATUS build_gfx_info_hob (struct lb_header *header)
{
	struct	lb_framebuffer			*lbFbRec;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION	*GfxMode;
	EFI_PEI_GRAPHICS_INFO_HOB		*GfxInfo;
	const	EFI_GUID			graphics_info_guid = EFI_PEI_GRAPHICS_INFO_HOB_GUID;

	lbFbRec = FindCbTag (header, LB_TAG_FRAMEBUFFER);
	if (!lbFbRec)
		return EFI_NOT_FOUND;

	GfxInfo = BuildGuidHob (&graphics_info_guid, sizeof(*GfxInfo));
	if (!GfxInfo)
		return EFI_NOT_FOUND;

	GfxMode = &GfxInfo->GraphicsMode;
	GfxMode->Version              = 0;
	GfxMode->HorizontalResolution = lbFbRec->x_resolution;
	GfxMode->VerticalResolution   = lbFbRec->y_resolution;
	GfxMode->PixelsPerScanLine    = (lbFbRec->bytes_per_line << 3) / lbFbRec->bits_per_pixel;
	if ((lbFbRec->red_mask_pos == 0) && (lbFbRec->green_mask_pos == 8) && (lbFbRec->blue_mask_pos == 16)) {
		GfxMode->PixelFormat = PixelRedGreenBlueReserved8BitPerColor;
	} else if ((lbFbRec->blue_mask_pos == 0) && (lbFbRec->green_mask_pos == 8) && (lbFbRec->red_mask_pos == 16)) {
		GfxMode->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
	}
	GfxMode->PixelInformation.RedMask	= ((1 << lbFbRec->red_mask_size)      - 1) << lbFbRec->red_mask_pos;
	GfxMode->PixelInformation.GreenMask	= ((1 << lbFbRec->green_mask_size)    - 1) << lbFbRec->green_mask_pos;
	GfxMode->PixelInformation.BlueMask	= ((1 << lbFbRec->blue_mask_size)     - 1) << lbFbRec->blue_mask_pos;
	GfxMode->PixelInformation.ReservedMask	= ((1 << lbFbRec->reserved_mask_size) - 1) << lbFbRec->reserved_mask_pos;

	GfxInfo->FrameBufferBase = lbFbRec->physical_address;
	GfxInfo->FrameBufferSize = lbFbRec->bytes_per_line *  lbFbRec->y_resolution;

	return EFI_SUCCESS;
}

typedef struct {
	UINT16	Reversion;
	BOOLEAN	UseMmio;
	UINT8	RegisterWidth;
	UINT32	BaudRate;
	UINT64	RegisterBase;
} SERIAL_PORT_INFO;

static EFI_STATUS build_serial_hob(struct lb_header *header)
{
	struct lb_serial	*lbSerial;
	SERIAL_PORT_INFO	*hob_serial;
	EFI_GUID		serial_guid = SERIAL_INFO_GUID;

	lbSerial = FindCbTag (header, LB_TAG_SERIAL);
	if (!lbSerial)
		return EFI_NOT_FOUND;

	hob_serial = BuildGuidHob (&serial_guid, sizeof(*hob_serial));
	if (!hob_serial)
		return EFI_NOT_FOUND;

	hob_serial->Reversion     = 0;
	if (lbSerial->type == LB_SERIAL_TYPE_MEMORY_MAPPED)
		hob_serial->UseMmio    = TRUE;
	else
		hob_serial->UseMmio    = FALSE;
	hob_serial->RegisterWidth = lbSerial->regwidth;
	hob_serial->BaudRate      = lbSerial->baud;
	hob_serial->RegisterBase  = lbSerial->baseaddr;

	return EFI_SUCCESS;
}


static EFI_STATUS build_acpi_hob(void)
{
	u64		acpi_base;
	TABLE_HOB	*acpi_hob;
	EFI_GUID	acpi_guid = EFI_ACPI_TABLE_GUID;

	acpi_base = (u64)(UINTN)cbmem_find(CBMEM_ID_ACPI);
	if (!acpi_base)
		return EFI_NOT_FOUND;

	acpi_hob = BuildGuidHob(&acpi_guid, sizeof (TABLE_HOB));
	if (!acpi_hob)
		return EFI_OUT_OF_RESOURCES;

	acpi_hob->TableAddress = acpi_base;

	return EFI_SUCCESS;
}


static EFI_STATUS build_smbios_hob (void)
{
	u64		smbios_base;
	TABLE_HOB	*smbios_hob;
	EFI_GUID	smbios_guid = SMBIOS3_TABLE_GUID;

	smbios_base = (u64) (UINTN) cbmem_find(CBMEM_ID_SMBIOS);
	if (!smbios_base)
		return EFI_NOT_FOUND;

	smbios_hob = BuildGuidHob (&smbios_guid, sizeof (TABLE_HOB));
	if (!smbios_hob)
		return EFI_OUT_OF_RESOURCES;

	smbios_hob->TableAddress = smbios_base;

	return EFI_SUCCESS;
}

static EFI_STATUS build_memory_resource_hob (struct lb_memory_range  *range)
{
	EFI_PHYSICAL_ADDRESS		Base;
	EFI_RESOURCE_TYPE		Type;
	UINT64				Size;
	EFI_RESOURCE_ATTRIBUTE_TYPE	Attribute;

	Type = (range->type == 1) ? EFI_RESOURCE_SYSTEM_MEMORY : EFI_RESOURCE_MEMORY_RESERVED;
	Base = unpack_lb64(range->start);
	Size = unpack_lb64(range->size);

	Attribute = EFI_RESOURCE_ATTRIBUTE_PRESENT | \
		    EFI_RESOURCE_ATTRIBUTE_INITIALIZED | \
		    EFI_RESOURCE_ATTRIBUTE_TESTED | \
		    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE | \
		    EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE | \
		    EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE | \
		    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE;

	if (Base >= BASE_4GB )
		Attribute &= ~EFI_RESOURCE_ATTRIBUTE_TESTED;

	BuildResourceDescriptorHob (Type, Attribute, (EFI_PHYSICAL_ADDRESS)Base, Size);

	return EFI_SUCCESS;
}

static EFI_STATUS build_memory_hobs (struct lb_header *header)
{
	struct	lb_memory	*rec;
	struct	lb_memory_range	*range;
	UINTN			index;

	rec = (struct lb_memory *)FindCbTag (header, LB_TAG_MEMORY);
	if (!rec)
		return EFI_NOT_FOUND;

	for (index = 0; index < MEM_RANGE_COUNT(rec); index++) {
		range = MEM_RANGE_PTR(rec, index);
		build_memory_resource_hob(range);
	}

	return EFI_SUCCESS;
}

/* It will build HOBs based on information from bootloaders. */
void *build_payload_hobs(void)
{
	EFI_HOB_HANDOFF_INFO_TABLE			*HobTable;
	void						*HobBase;
	struct				lb_header	*header;
	EFI_RESOURCE_ATTRIBUTE_TYPE			ResourceAttribute;

	header		= (struct lb_header *)cbmem_find(CBMEM_ID_CBTABLE);
	HobBase		= cbmem_add(CBMEM_ID_HOB_POINTER, 0x4000);
	HobTable	= HobTableInit(HobBase, 0x4000, HobBase, (u8 *)HobBase + 0x40000);

	build_gfx_info_hob(header);
	build_serial_hob(header);
	build_memory_hobs(header);

	/* Hard code for now */
	BuildCpuHob (36, 16);

	build_acpi_hob ();
	build_smbios_hob ();

	/* Report Local APIC range */
	ResourceAttribute = EFI_RESOURCE_ATTRIBUTE_PRESENT | EFI_RESOURCE_ATTRIBUTE_INITIALIZED |\
			    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE | EFI_RESOURCE_ATTRIBUTE_TESTED;
	BuildResourceDescriptorHob (EFI_RESOURCE_MEMORY_MAPPED_IO, ResourceAttribute, 0xFEC80000, SIZE_512KB);
	BuildMemoryAllocationHob ( 0xFEC80000, SIZE_512KB, EfiMemoryMappedIO);

	return HobTable;
}
