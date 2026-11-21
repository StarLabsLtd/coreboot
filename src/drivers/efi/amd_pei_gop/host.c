/* SPDX-License-Identifier: GPL-2.0-only */
/* PI services required by the AMD IA32 GOP PEIM. */
#define EFIAPI __attribute__((cdecl, regparm(0)))
#include <PiPei.h>
#include <Pi/PiHob.h>
#include <Ppi/Graphics.h>
#include <Ppi/Pcd.h>
#include <Ppi/PiPcd.h>
#include "pe32.h"
#include "context.h"
#include "gop_init.h"
extern CONST UINT8 _binary_amd_gop_ffs_start[], _binary_amd_gop_ffs_end[];
#define amd_gop_ffs_start _binary_amd_gop_ffs_start
#define amd_gop_ffs_end   _binary_amd_gop_ffs_end

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static UINTN pool_start, pool_end;
static UINTN image_base, image_capacity;
static struct gop_context *active_context;

static EFI_PEI_SERVICES services;
static CONST EFI_PEI_SERVICES *services_pointer = &services;
static CONST EFI_PEI_PPI_DESCRIPTOR *ppis[32];
static CONST EFI_PEI_NOTIFY_DESCRIPTOR *notifications[16];
static UINTN ppi_count, notify_count, pool_next;
static UINT64 hobs[2048];
static UINTN hob_end;

static void log_text(const char *text)
{
	while (*text) {
		char value = *text++;
		if (active_context) {
			if (active_context->trace_used < active_context->trace.size)
				((char *)(UINTN)active_context->trace
					 .base)[active_context->trace_used++] = value;
			else if (active_context->trace_dropped != ~0U)
				active_context->trace_dropped++;
		}
		/* Native probe records only into the owned RAM trace. */
	}
}

static void log_hex(UINT32 value)
{
	static const char digits[] = "0123456789abcdef";
	for (int i = 28; i >= 0; i -= 4) {
		char ch = digits[(value >> i) & 15];
		char text[2] = {ch, 0};
		log_text(text);
	}
	log_text("\n");
}

static void __attribute__((noreturn)) fail(const char *message)
{
	log_text(message);
	/* The independent EC timer recovers a fault; no debug-port writes. */
	for (;;)
		asm volatile("hlt");
}

static VOID EFIAPI copy_memory(VOID *dst, VOID *src, UINTN size)
{
	UINT8 *d = dst, *s = src;
	while (size--)
		*d++ = *s++;
}

static VOID EFIAPI fill_memory(VOID *dst, UINTN size, UINT8 value)
{
	UINT8 *d = dst;
	while (size--)
		*d++ = value;
}

static BOOLEAN same_guid(CONST EFI_GUID *a, CONST EFI_GUID *b)
{
	CONST UINT8 *x = (CONST VOID *)a, *y = (CONST VOID *)b;
	if (!a || !b)
		return FALSE;
	for (UINTN i = 0; i < sizeof(*a); i++)
		if (x[i] != y[i])
			return FALSE;
	return TRUE;
}

static void notify_one(CONST EFI_PEI_PPI_DESCRIPTOR *ppi)
{
	UINTN count = notify_count;
	for (UINTN i = 0; i < count; i++) {
		CONST EFI_PEI_NOTIFY_DESCRIPTOR *n = notifications[i];
		if (same_guid(ppi->Guid, n->Guid))
			n->Notify((EFI_PEI_SERVICES **)&services_pointer,
				  (EFI_PEI_NOTIFY_DESCRIPTOR *)n, ppi->Ppi);
	}
}

static EFI_STATUS EFIAPI install_ppi(CONST EFI_PEI_SERVICES **pei,
				     CONST EFI_PEI_PPI_DESCRIPTOR *list)
{
	UINTN count = 0;
	if (!list)
		return EFI_INVALID_PARAMETER;
	for (;;) {
		if (count >= ARRAY_SIZE(ppis) - ppi_count)
			return EFI_OUT_OF_RESOURCES;
		if (!(list[count].Flags & EFI_PEI_PPI_DESCRIPTOR_PPI) || !list[count].Guid)
			return EFI_INVALID_PARAMETER;
		if (list[count++].Flags & EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST)
			break;
	}
	log_text("InstallPpi ");
	log_hex(list->Guid->Data1);
	UINTN first = ppi_count;
	for (UINTN i = 0; i < count; i++)
		ppis[ppi_count++] = &list[i];
	for (UINTN i = 0; i < count; i++)
		notify_one(ppis[first + i]);
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI locate_ppi(CONST EFI_PEI_SERVICES **pei, CONST EFI_GUID *guid,
				    UINTN instance, EFI_PEI_PPI_DESCRIPTOR **descriptor,
				    VOID **ppi)
{
	if (!guid || !ppi)
		return EFI_INVALID_PARAMETER;
	log_text("LocatePpi ");
	log_hex(guid->Data1);
	for (UINTN i = 0; i < ppi_count; i++) {
		if (!same_guid(guid, ppis[i]->Guid))
			continue;
		if (instance--)
			continue;
		if (descriptor)
			*descriptor = (EFI_PEI_PPI_DESCRIPTOR *)ppis[i];
		*ppi = ppis[i]->Ppi;
		return EFI_SUCCESS;
	}
	return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI reinstall_ppi(CONST EFI_PEI_SERVICES **pei,
				       CONST EFI_PEI_PPI_DESCRIPTOR *old,
				       CONST EFI_PEI_PPI_DESCRIPTOR *new)
{
	if (!old || !new || !(new->Flags & EFI_PEI_PPI_DESCRIPTOR_PPI) ||
	    !same_guid(old->Guid, new->Guid))
		return EFI_INVALID_PARAMETER;
	for (UINTN i = 0; i < ppi_count; i++) {
		if (ppis[i] == old) {
			ppis[i] = new;
			notify_one(new);
			return EFI_SUCCESS;
		}
	}
	return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI notify_ppi(CONST EFI_PEI_SERVICES **pei,
				    CONST EFI_PEI_NOTIFY_DESCRIPTOR *list)
{
	UINTN count = 0;
	if (!list)
		return EFI_INVALID_PARAMETER;
	for (;;) {
		if (count >= ARRAY_SIZE(notifications) - notify_count)
			return EFI_OUT_OF_RESOURCES;
		if (!list[count].Guid || !list[count].Notify)
			return EFI_INVALID_PARAMETER;
		if (!(list[count].Flags & EFI_PEI_PPI_DESCRIPTOR_NOTIFY_CALLBACK) ||
		    (list[count].Flags & EFI_PEI_PPI_DESCRIPTOR_NOTIFY_DISPATCH))
			return EFI_UNSUPPORTED;
		if (list[count++].Flags & EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST)
			break;
	}
	for (UINTN i = 0; i < count; i++) {
		notifications[notify_count++] = &list[i];
		UINTN installed = ppi_count;
		for (UINTN j = 0; j < installed; j++)
			if (same_guid(list[i].Guid, ppis[j]->Guid))
				list[i].Notify((EFI_PEI_SERVICES **)pei,
					       (EFI_PEI_NOTIFY_DESCRIPTOR *)&list[i],
					       ppis[j]->Ppi);
	}
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI get_boot_mode(CONST EFI_PEI_SERVICES **pei, EFI_BOOT_MODE *mode)
{
	if (!mode)
		return EFI_INVALID_PARAMETER;
	*mode = BOOT_WITH_FULL_CONFIGURATION;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI get_hobs(CONST EFI_PEI_SERVICES **pei, VOID **list)
{
	if (!list)
		return EFI_INVALID_PARAMETER;
	*list = hobs;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI create_hob(CONST EFI_PEI_SERVICES **pei, UINT16 type, UINT16 length,
				    VOID **result)
{
	UINTN size = ((UINTN)length + 7) & ~7U;
	if (!result || type == EFI_HOB_TYPE_END_OF_HOB_LIST ||
	    length < sizeof(EFI_HOB_GENERIC_HEADER) || size > 0xfff8)
		return EFI_INVALID_PARAMETER;
	if (size > sizeof(hobs) - sizeof(EFI_HOB_GENERIC_HEADER) - hob_end)
		return EFI_OUT_OF_RESOURCES;
	EFI_HOB_GENERIC_HEADER *hob = (VOID *)((UINT8 *)hobs + hob_end);
	fill_memory(hob, size + sizeof(*hob), 0);
	hob->HobType = type;
	hob->HobLength = size;
	hob_end += size;
	EFI_HOB_GENERIC_HEADER *end = (VOID *)((UINT8 *)hobs + hob_end);
	end->HobType = EFI_HOB_TYPE_END_OF_HOB_LIST;
	end->HobLength = sizeof(*end);
	((EFI_HOB_HANDOFF_INFO_TABLE *)hobs)->EfiEndOfHobList = (UINTN)end;
	*result = hob;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI allocate_pool(CONST EFI_PEI_SERVICES **pei, UINTN size, VOID **result)
{
	UINTN start = (pool_next + 7) & ~7U;
	if (!result)
		return EFI_INVALID_PARAMETER;
	if (size > pool_end - start)
		return EFI_OUT_OF_RESOURCES;
	*result = (VOID *)start;
	pool_next = start + size;
	((EFI_HOB_HANDOFF_INFO_TABLE *)hobs)->EfiFreeMemoryBottom = pool_next;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI allocate_pages(CONST EFI_PEI_SERVICES **pei, EFI_MEMORY_TYPE type,
					UINTN pages, EFI_PHYSICAL_ADDRESS *result)
{
	UINTN start = (pool_next + 4095) & ~4095U;
	switch (type) {
	case EfiReservedMemoryType:
	case EfiLoaderCode:
	case EfiLoaderData:
	case EfiBootServicesCode:
	case EfiBootServicesData:
	case EfiRuntimeServicesCode:
	case EfiRuntimeServicesData:
	case EfiACPIReclaimMemory:
	case EfiACPIMemoryNVS:
		break;
	default:
		return EFI_INVALID_PARAMETER;
	}
	if (!result || !pages)
		return EFI_INVALID_PARAMETER;
	if (pages > (pool_end - start) / 4096)
		return EFI_OUT_OF_RESOURCES;
	EFI_HOB_MEMORY_ALLOCATION *hob;
	EFI_STATUS ret =
		create_hob(pei, EFI_HOB_TYPE_MEMORY_ALLOCATION, sizeof(*hob), (VOID **)&hob);
	if (EFI_ERROR(ret))
		return ret;
	hob->AllocDescriptor.MemoryBaseAddress = start;
	hob->AllocDescriptor.MemoryLength = pages * 4096;
	hob->AllocDescriptor.MemoryType = type;
	*result = start;
	pool_next = start + pages * 4096;
	((EFI_HOB_HANDOFF_INFO_TABLE *)hobs)->EfiFreeMemoryBottom = pool_next;
	return EFI_SUCCESS;
}

struct idtr {
	UINT16 limit;
	UINT32 base;
} __attribute__((packed));
struct gate {
	UINT16 low, selector;
	UINT8 zero, flags;
	UINT16 high;
} __attribute__((packed));
static struct {
	UINT32 padding;
	CONST EFI_PEI_SERVICES **pointer;
	struct gate gates[256];
} private_idt __attribute__((aligned(16)));

static void __attribute__((noreturn)) fault(void)
{
	fail("IDT fault\n");
}

static void install_idt(struct idtr *saved)
{
	UINT16 selector;
	asm volatile("sidt %0" : "=m"(*saved));
	asm volatile("mov %%cs, %0" : "=r"(selector));
	private_idt.pointer = &services_pointer;
	for (UINTN i = 0; i < ARRAY_SIZE(private_idt.gates); i++) {
		private_idt.gates[i] = (struct gate){
			.low = (UINTN)fault & 0xffff,
			.selector = selector,
			.flags = 0x8e,
			.high = (UINTN)fault >> 16,
		};
	}
	struct idtr next = {sizeof(private_idt.gates) - 1, (UINTN)private_idt.gates};
	asm volatile("lidt %0" ::"m"(next) : "memory");
}

static EFI_GUID pcd_guid = PCD_PPI_GUID;
static EFI_GUID pi_pcd_guid = EFI_PEI_PCD_PPI_GUID;
static EFI_GUID nbio_guid = {
	0xed92898e,
	0xb59a,
	0x4647,
	{0x88, 0xcc, 0xdb, 0xeb, 0xa3, 0xb4, 0xa6, 0xcd}
};
static EFI_GUID graphics_guid = EFI_PEI_GRAPHICS_PPI_GUID;
static EFI_GUID amd_pcd_guid = {
	0xd4d8435f,
	0xfffb,
	0x4acb,
	{0xa0, 0x4d, 0xff, 0x0f, 0xad, 0x67, 0x7f, 0xe9}
};

static UINT32 EFIAPI pcd_get32(CONST EFI_GUID *guid, UINTN token)
{
	log_text("PCD Get32Ex ");
	log_hex(token);
	if (!same_guid(guid, &amd_pcd_guid))
		fail("Unsupported PCD namespace\n");
	/* The caller supplies the actual openSIL UMA range, in PCD units. */
	switch (token) {
	case 0x50000:
		return active_context->uma_size >> 20;
	case 0x50001:
		return active_context->uma_base >> 24;
	case 0x50002:
		return (active_context->uma_base + active_context->uma_size - 1) >> 24;
	default:
		fail("Unsupported PCD token\n");
	}
}
static VOID EFIAPI set_sku(UINTN sku)
{
	if (sku)
		fail("Unsupported PCD SKU\n");
}
static PCD_PPI pcd = {.SetSku = set_sku, .Get32Ex = pcd_get32};
static EFI_PEI_PCD_PPI pi_pcd = {.SetSku = set_sku, .Get32 = pcd_get32};
static EFI_PEI_PPI_DESCRIPTOR initial_ppis[] = {
	{EFI_PEI_PPI_DESCRIPTOR_PPI,					 &pcd_guid,    &pcd   },
	{EFI_PEI_PPI_DESCRIPTOR_PPI,					 &pi_pcd_guid, &pi_pcd},
	{EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST, &nbio_guid,   NULL   },
};
static EFI_STATUS EFIAPI report_code(CONST EFI_PEI_SERVICES **pei, EFI_STATUS_CODE_TYPE type,
				     EFI_STATUS_CODE_VALUE value, UINT32 instance,
				     CONST EFI_GUID *caller, CONST EFI_STATUS_CODE_DATA *data)
{
	log_text("PI status ");
	log_hex(type);
	log_hex(value);
	return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI register_shadow(EFI_PEI_FILE_HANDLE file)
{
	return file == (VOID *)amd_gop_ffs_start ? EFI_ALREADY_STARTED : EFI_INVALID_PARAMETER;
}
static EFI_STATUS EFIAPI find_volume(CONST EFI_PEI_SERVICES **pei, UINTN instance,
				     EFI_PEI_FV_HANDLE *volume)
{
	if (!volume)
		return EFI_INVALID_PARAMETER;
	*volume = NULL;
	return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI find_section(CONST EFI_PEI_SERVICES **pei, EFI_SECTION_TYPE type,
				      EFI_PEI_FILE_HANDLE file, VOID **data)
{
	if (!data)
		return EFI_INVALID_PARAMETER;
	if (file != (VOID *)amd_gop_ffs_start || type != EFI_SECTION_PE32)
		return EFI_NOT_FOUND;
	*data = (VOID *)(amd_gop_ffs_start + 28);
	return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_info(EFI_PEI_FILE_HANDLE file, EFI_FV_FILE_INFO *info)
{
	if (file != (VOID *)amd_gop_ffs_start || !info)
		return EFI_INVALID_PARAMETER;
	fill_memory(info, sizeof(*info), 0);
	copy_memory(&info->FileName, (VOID *)amd_gop_ffs_start, sizeof(EFI_GUID));
	info->FileType = EFI_FV_FILETYPE_PEIM;
	info->Buffer = (VOID *)(amd_gop_ffs_start + 24);
	info->BufferSize = amd_gop_ffs_end - amd_gop_ffs_start - 24;
	return EFI_SUCCESS;
}

static void initialize(void)
{
	fill_memory(&services, sizeof(services), 0);
	services.Hdr.Signature = PEI_SERVICES_SIGNATURE;
	services.Hdr.Revision = PEI_SERVICES_REVISION;
	services.Hdr.HeaderSize = sizeof(services);
	services.InstallPpi = install_ppi;
	services.ReInstallPpi = reinstall_ppi;
	services.LocatePpi = locate_ppi;
	services.NotifyPpi = notify_ppi;
	services.GetBootMode = get_boot_mode;
	services.GetHobList = get_hobs;
	services.CreateHob = create_hob;
	services.AllocatePool = allocate_pool;
	services.AllocatePages = allocate_pages;
	services.CopyMem = copy_memory;
	services.SetMem = fill_memory;
	services.ReportStatusCode = report_code;
	services.RegisterForShadow = register_shadow;
	services.FfsFindNextVolume = find_volume;
	services.FfsFindSectionData = find_section;
	services.FfsGetFileInfo = file_info;
	fill_memory(hobs, sizeof(hobs), 0);
	EFI_HOB_HANDOFF_INFO_TABLE *phit = (VOID *)hobs;
	phit->Header.HobType = EFI_HOB_TYPE_HANDOFF;
	phit->Header.HobLength = sizeof(*phit);
	phit->Version = EFI_HOB_HANDOFF_TABLE_VERSION;
	phit->BootMode = BOOT_WITH_FULL_CONFIGURATION;
	phit->EfiMemoryBottom = pool_start;
	phit->EfiMemoryTop = pool_end;
	phit->EfiFreeMemoryBottom = pool_start;
	phit->EfiFreeMemoryTop = pool_end;
	hob_end = sizeof(*phit);
	EFI_HOB_GENERIC_HEADER *end = (VOID *)((UINT8 *)hobs + hob_end);
	end->HobType = EFI_HOB_TYPE_END_OF_HOB_LIST;
	end->HobLength = sizeof(*end);
	phit->EfiEndOfHobList = (UINTN)end;
}

UINT32 EFIAPI _start(VOID *context, struct gop_init_policy *policy, struct gop_mode *output);

UINT32 EFIAPI _start(VOID *context, struct gop_init_policy *policy, struct gop_mode *output)
{
	struct gop_context *c = context;
	if (!gop_context_valid(c))
		return EFI_INVALID_PARAMETER;
	UINTN object = (UINTN)context;
	if (object < c->stack.base || object > c->stack.base + c->stack.size - sizeof(*c))
		return EFI_INVALID_PARAMETER;
	active_context = c;
	pool_start = c->pool.base;
	pool_end = pool_start + c->pool.size;
	pool_next = pool_start;
	image_base = c->image.base;
	image_capacity = c->image.size;
	if (policy || output) {
		UINTN p = (UINTN)policy, o = (UINTN)output;
		if (!policy || !output || p < c->stack.base || o < c->stack.base ||
		    p > c->stack.base + c->stack.size - sizeof(*policy) ||
		    o > c->stack.base + c->stack.size - sizeof(*output) ||
		    (p < object + sizeof(*c) && object < p + sizeof(*policy)) ||
		    (o < object + sizeof(*c) && object < o + sizeof(*output)) ||
		    (p < o + sizeof(*output) && o < p + sizeof(*policy)) ||
		    policy->length != sizeof(*policy) || policy->format != 1 ||
		    policy->content != 1 || !policy->width || !policy->height ||
		    policy->width > 16384 || policy->height > 16384 ||
		    policy->vbios != pool_end - 0x10000)
			return EFI_INVALID_PARAMETER;
		/* The caller copied the immutable asset into this reserved pool tail. */
		pool_end -= 0x10000;
		fill_memory(output, sizeof(*output), 0);
	}
	initialize();
	log_text(policy ? "AMD PEI graphics initialization\n" : "AMD PEI graphics dispatch\n");
	EFI_STATUS status = install_ppi(&services_pointer, initial_ppis);
	if (EFI_ERROR(status))
		return status;
	UINTN entry = load_pe32(amd_gop_ffs_start + 28,
				amd_gop_ffs_end - amd_gop_ffs_start - 28, (VOID *)image_base,
				image_capacity);
	if (!entry)
		return EFI_LOAD_ERROR;
	struct idtr saved;
	install_idt(&saved);
	status = ((EFI_PEIM_ENTRY_POINT2)entry)((VOID *)amd_gop_ffs_start, &services_pointer);
	log_text("PEIM status ");
	log_hex(status);
	if (!EFI_ERROR(status)) {
		EFI_PEI_GRAPHICS_PPI *graphics = NULL;
		status = locate_ppi(&services_pointer, &graphics_guid, 0, NULL,
				    (VOID **)&graphics);
		if (!EFI_ERROR(status) && graphics) {
			EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;
			fill_memory(&mode, sizeof(mode), 0);
			if (policy) {
				log_text("GraphicsPpiInit entering\n");
				status = graphics->GraphicsPpiInit(policy);
				log_text("GraphicsPpiInit status ");
				log_hex(status);
				if (EFI_ERROR(status))
					goto restore;
			}
			status = graphics->GraphicsPpiGetMode(&mode);
			log_text(policy ? "Initialized mode status " : "Pre-init mode status ");
			log_hex(status);
			if (policy && !EFI_ERROR(status)) {
				UINTN info = (UINTN)mode.Info;
				BOOLEAN owned_info = FALSE;
				const struct gop_range32 ranges[] = {c->helper, c->pool,
								     c->image};
				for (UINTN i = 0; i < ARRAY_SIZE(ranges); i++)
					if (info >= ranges[i].base &&
					    sizeof(*mode.Info) <= ranges[i].size &&
					    info - ranges[i].base <=
						    ranges[i].size - sizeof(*mode.Info))
						owned_info = TRUE;
				if (!owned_info || mode.SizeOfInfo < sizeof(*mode.Info)) {
					status = EFI_PROTOCOL_ERROR;
					goto restore;
				}
				output->framebuffer = mode.FrameBufferBase;
				output->framebuffer_size = mode.FrameBufferSize;
				output->width = mode.Info->HorizontalResolution;
				output->height = mode.Info->VerticalResolution;
				output->scanline = mode.Info->PixelsPerScanLine;
				output->pixel_format = mode.Info->PixelFormat;
				output->red = mode.Info->PixelInformation.RedMask;
				output->green = mode.Info->PixelInformation.GreenMask;
				output->blue = mode.Info->PixelInformation.BlueMask;
				output->reserved = mode.Info->PixelInformation.ReservedMask;
				log_text("Initialized mode captured\n");
			} else if (!policy && status == EFI_NOT_READY) {
				status = EFI_SUCCESS;

				log_text(
					"Dispatch completed without graphics initialization\n");
			} else
				status = EFI_PROTOCOL_ERROR;
		} else
			status = EFI_NOT_FOUND;
	}
restore:
	asm volatile("lidt %0" ::"m"(saved) : "memory");
	return status;
}
