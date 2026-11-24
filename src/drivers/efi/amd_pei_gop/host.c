/* SPDX-License-Identifier: GPL-2.0-only */
/* PI services required by the AMD IA32 GOP PEIM. */
#define EFIAPI __attribute__((cdecl, regparm(0)))
#include <commonlib/bsd/compiler.h>
#include <commonlib/bsd/helpers.h>
#include <PiPei.h>
#include <Pi/PiHob.h>
#include <Ppi/Graphics.h>
#include <Ppi/Pcd.h>
#include <Ppi/PiPcd.h>
#include "context.h"
#include "pe32.h"
#include "gop_init.h"
extern const UINT8 _binary_amd_gop_ffs_start[], _binary_amd_gop_ffs_end[];
#define amd_gop_ffs_start _binary_amd_gop_ffs_start
#define amd_gop_ffs_end   _binary_amd_gop_ffs_end

static UINTN pool_start, pool_end;
static struct gop_context *active_context;

static EFI_PEI_SERVICES services;
static const EFI_PEI_SERVICES *services_pointer = &services;
static const EFI_PEI_PPI_DESCRIPTOR *ppis[32];
static const EFI_PEI_NOTIFY_DESCRIPTOR *notifications[16];
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

static void __noreturn fail(const char *message)
{
	log_text(message);
	/* The independent EC timer recovers a fault; no debug-port writes. */
	for (;;)
		asm volatile("hlt");
}

static void EFIAPI copy_memory(void *dst, void *src, UINTN size)
{
	UINT8 *d = dst, *s = src;
	while (size--)
		*d++ = *s++;
}

static void EFIAPI fill_memory(void *dst, UINTN size, UINT8 value)
{
	UINT8 *d = dst;
	while (size--)
		*d++ = value;
}

static BOOLEAN same_guid(const EFI_GUID *a, const EFI_GUID *b)
{
	const UINT8 *x = (const void *)a, *y = (const void *)b;
	if (!a || !b)
		return FALSE;
	for (UINTN i = 0; i < sizeof(*a); i++)
		if (x[i] != y[i])
			return FALSE;
	return TRUE;
}

static void notify_one(const EFI_PEI_PPI_DESCRIPTOR *ppi)
{
	UINTN count = notify_count;
	for (UINTN i = 0; i < count; i++) {
		const EFI_PEI_NOTIFY_DESCRIPTOR *n = notifications[i];
		if (same_guid(ppi->Guid, n->Guid))
			n->Notify((EFI_PEI_SERVICES **)&services_pointer,
				  (EFI_PEI_NOTIFY_DESCRIPTOR *)n, ppi->Ppi);
	}
}

static EFI_STATUS EFIAPI install_ppi(const EFI_PEI_SERVICES **pei,
				     const EFI_PEI_PPI_DESCRIPTOR *list)
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

static EFI_STATUS EFIAPI locate_ppi(const EFI_PEI_SERVICES **pei, const EFI_GUID *guid,
				    UINTN instance, EFI_PEI_PPI_DESCRIPTOR **descriptor,
				    void **ppi)
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

static EFI_STATUS EFIAPI reinstall_ppi(const EFI_PEI_SERVICES **pei,
				       const EFI_PEI_PPI_DESCRIPTOR *old,
				       const EFI_PEI_PPI_DESCRIPTOR *new)
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

static EFI_STATUS EFIAPI notify_ppi(const EFI_PEI_SERVICES **pei,
				    const EFI_PEI_NOTIFY_DESCRIPTOR *list)
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

static EFI_STATUS EFIAPI get_boot_mode(const EFI_PEI_SERVICES **pei, EFI_BOOT_MODE *mode)
{
	if (!mode)
		return EFI_INVALID_PARAMETER;
	*mode = BOOT_WITH_FULL_CONFIGURATION;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI get_hobs(const EFI_PEI_SERVICES **pei, void **list)
{
	if (!list)
		return EFI_INVALID_PARAMETER;
	*list = hobs;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI create_hob(const EFI_PEI_SERVICES **pei, UINT16 type, UINT16 length,
				    void **result)
{
	UINTN size = ((UINTN)length + 7) & ~7U;
	if (!result || type == EFI_HOB_TYPE_END_OF_HOB_LIST ||
	    length < sizeof(EFI_HOB_GENERIC_HEADER) || size > 0xfff8)
		return EFI_INVALID_PARAMETER;
	if (size > sizeof(hobs) - sizeof(EFI_HOB_GENERIC_HEADER) - hob_end)
		return EFI_OUT_OF_RESOURCES;
	EFI_HOB_GENERIC_HEADER *hob = (void *)((UINT8 *)hobs + hob_end);
	fill_memory(hob, size + sizeof(*hob), 0);
	hob->HobType = type;
	hob->HobLength = size;
	hob_end += size;
	EFI_HOB_GENERIC_HEADER *end = (void *)((UINT8 *)hobs + hob_end);
	end->HobType = EFI_HOB_TYPE_END_OF_HOB_LIST;
	end->HobLength = sizeof(*end);
	((EFI_HOB_HANDOFF_INFO_TABLE *)hobs)->EfiEndOfHobList = (UINTN)end;
	*result = hob;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI allocate_pool(const EFI_PEI_SERVICES **pei, UINTN size, void **result)
{
	UINTN start = (pool_next + 7) & ~7U;
	if (!result)
		return EFI_INVALID_PARAMETER;
	if (size > pool_end - start)
		return EFI_OUT_OF_RESOURCES;
	*result = (void *)start;
	pool_next = start + size;
	((EFI_HOB_HANDOFF_INFO_TABLE *)hobs)->EfiFreeMemoryBottom = pool_next;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI allocate_pages(const EFI_PEI_SERVICES **pei, EFI_MEMORY_TYPE type,
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
		create_hob(pei, EFI_HOB_TYPE_MEMORY_ALLOCATION, sizeof(*hob), (void **)&hob);
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
} __packed;
struct gate {
	UINT16 low, selector;
	UINT8 zero, flags;
	UINT16 high;
} __packed;
static struct {
	UINT32 padding;
	const EFI_PEI_SERVICES **pointer;
	struct gate gates[256];
} private_idt __aligned(16);

static void __noreturn fault(void)
{
	fail("Processor exception in GOP host\n");
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

static UINT32 EFIAPI pcd_get32(const EFI_GUID *guid, UINTN token)
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
static void EFIAPI set_sku(UINTN sku)
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
static EFI_STATUS EFIAPI report_code(const EFI_PEI_SERVICES **pei, EFI_STATUS_CODE_TYPE type,
				     EFI_STATUS_CODE_VALUE value, UINT32 instance,
				     const EFI_GUID *caller, const EFI_STATUS_CODE_DATA *data)
{
	log_text("PI status ");
	log_hex(type);
	log_hex(value);
	return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI register_shadow(EFI_PEI_FILE_HANDLE file)
{
	return file == (void *)amd_gop_ffs_start ? EFI_ALREADY_STARTED : EFI_INVALID_PARAMETER;
}
static EFI_STATUS EFIAPI find_volume(const EFI_PEI_SERVICES **pei, UINTN instance,
				     EFI_PEI_FV_HANDLE *volume)
{
	if (!volume)
		return EFI_INVALID_PARAMETER;
	*volume = NULL;
	return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI find_section(const EFI_PEI_SERVICES **pei, EFI_SECTION_TYPE type,
				      EFI_PEI_FILE_HANDLE file, void **data)
{
	if (!data)
		return EFI_INVALID_PARAMETER;
	if (file != (void *)amd_gop_ffs_start || type != EFI_SECTION_PE32)
		return EFI_NOT_FOUND;
	*data = (void *)(amd_gop_ffs_start + 28);
	return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_info(EFI_PEI_FILE_HANDLE file, EFI_FV_FILE_INFO *info)
{
	if (file != (void *)amd_gop_ffs_start || !info)
		return EFI_INVALID_PARAMETER;
	fill_memory(info, sizeof(*info), 0);
	copy_memory(&info->FileName, (void *)amd_gop_ffs_start, sizeof(EFI_GUID));
	info->FileType = EFI_FV_FILETYPE_PEIM;
	info->Buffer = (void *)(amd_gop_ffs_start + 24);
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
	EFI_HOB_HANDOFF_INFO_TABLE *phit = (void *)hobs;
	phit->Header.HobType = EFI_HOB_TYPE_HANDOFF;
	phit->Header.HobLength = sizeof(*phit);
	phit->Version = EFI_HOB_HANDOFF_TABLE_VERSION;
	phit->BootMode = BOOT_WITH_FULL_CONFIGURATION;
	phit->EfiMemoryBottom = pool_start;
	phit->EfiMemoryTop = pool_end;
	phit->EfiFreeMemoryBottom = pool_start;
	phit->EfiFreeMemoryTop = pool_end;
	hob_end = sizeof(*phit);
	EFI_HOB_GENERIC_HEADER *end = (void *)((UINT8 *)hobs + hob_end);
	end->HobType = EFI_HOB_TYPE_END_OF_HOB_LIST;
	end->HobLength = sizeof(*end);
	phit->EfiEndOfHobList = (UINTN)end;
}

static EFI_STATUS read_mode(const EFI_PEI_GRAPHICS_PPI *graphics, struct gop_mode *output)
{
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;
	const struct gop_range32 ranges[] = {
		active_context->helper, active_context->pool, active_context->image,
	};

	fill_memory(&mode, sizeof(mode), 0);
	EFI_STATUS status = graphics->GraphicsPpiGetMode(&mode);
	if (EFI_ERROR(status))
		return status;
	if (mode.SizeOfInfo < sizeof(*mode.Info))
		return EFI_PROTOCOL_ERROR;

	UINTN address = (UINTN)mode.Info;
	for (unsigned int i = 0; i < ARRAY_SIZE(ranges); i++) {
		if (address < ranges[i].base || sizeof(*mode.Info) > ranges[i].size ||
		    address - ranges[i].base > ranges[i].size - sizeof(*mode.Info))
			continue;
		*output = (struct gop_mode) {
			.framebuffer = mode.FrameBufferBase,
			.framebuffer_size = mode.FrameBufferSize,
			.width = mode.Info->HorizontalResolution,
			.height = mode.Info->VerticalResolution,
			.scanline = mode.Info->PixelsPerScanLine,
			.pixel_format = mode.Info->PixelFormat,
		};
		return EFI_SUCCESS;
	}
	return EFI_PROTOCOL_ERROR;
}

static EFI_STATUS run_peim(UINTN entry, struct gop_init_policy *policy, struct gop_mode *output)
{
	EFI_STATUS status = ((EFI_PEIM_ENTRY_POINT2)entry)((void *)amd_gop_ffs_start,
							 &services_pointer);
	log_text("PEIM status ");
	log_hex(status);
	if (EFI_ERROR(status))
		return status;

	EFI_PEI_GRAPHICS_PPI *graphics;
	status = locate_ppi(&services_pointer, &graphics_guid, 0, NULL, (void **)&graphics);
	if (EFI_ERROR(status))
		return status;
	if (!graphics || !graphics->GraphicsPpiInit || !graphics->GraphicsPpiGetMode)
		return EFI_PROTOCOL_ERROR;

	status = graphics->GraphicsPpiInit(policy);
	log_text("GraphicsPpiInit status ");
	log_hex(status);
	if (EFI_ERROR(status))
		return status;

	return read_mode(graphics, output);
}

UINT32 EFIAPI _start(void *context, struct gop_init_policy *policy, struct gop_mode *output);

UINT32 EFIAPI _start(void *context, struct gop_init_policy *policy, struct gop_mode *output)
{
	struct gop_context *c = context;
	if (!gop_context_valid(c))
		return EFI_INVALID_PARAMETER;

	UINTN object = (UINTN)context, p = (UINTN)policy, o = (UINTN)output;
	UINTN stack_base = c->stack.base, stack_end = stack_base + c->stack.size;
	if (!policy || !output || c->stack.size < sizeof(*c) ||
	    object < stack_base || object > stack_end - sizeof(*c) ||
	    p < stack_base || p > stack_end - sizeof(*policy) ||
	    o < stack_base || o > stack_end - sizeof(*output) ||
	    (p < object + sizeof(*c) && object < p + sizeof(*policy)) ||
	    (o < object + sizeof(*c) && object < o + sizeof(*output)) ||
	    (p < o + sizeof(*output) && o < p + sizeof(*policy)) ||
	    policy->length != sizeof(*policy) || policy->format != 1 ||
	    policy->content != 1 || (!policy->width != !policy->height) ||
	    policy->width > 16384 || policy->height > 16384)
		return EFI_INVALID_PARAMETER;

	/* The caller copied the VBIOS into the reserved pool tail. */
	UINTN pool_limit = c->pool.base + c->pool.size - 0x10000;
	if (policy->vbios != pool_limit)
		return EFI_INVALID_PARAMETER;

	active_context = c;
	pool_start = c->pool.base;
	pool_end = pool_limit;
	pool_next = pool_start;
	initialize();
	fill_memory(output, sizeof(*output), 0);

	EFI_STATUS status = install_ppi(&services_pointer, initial_ppis);
	if (EFI_ERROR(status))
		return status;
	UINTN entry = load_pe32(amd_gop_ffs_start + 28,
				amd_gop_ffs_end - amd_gop_ffs_start - 28,
				(void *)(UINTN)c->image.base, c->image.size);
	if (!entry)
		return EFI_LOAD_ERROR;

	struct idtr saved;
	install_idt(&saved);
	status = run_peim(entry, policy, output);
	asm volatile("lidt %0" :: "m"(saved) : "memory");
	return status;
}
