/* SPDX-License-Identifier: GPL-2.0-only */
/* openSIL prepares UMA; the PI host initializes graphics before table handoff. */
#include <acpi/acpi.h>
#include <bootstate.h>
#include <cbfs.h>
#include <cbmem.h>
#include <commonlib/endian.h>
#include <console/console.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <device/pci_rom.h>
#include <device/resource.h>
#include <drivers/efi/amd_pei_gop/context.h>
#include <drivers/efi/amd_pei_gop/gop_init.h>
#include <drivers/efi/amd_pei_gop/loader.h>
#include <framebuffer_info.h>
#include <GFX/GfxClass-api.h>
#include <mode_switch.h>
#include <Sil-api.h>
#include <SilCommon.h>
#include <soc/pci_devs.h>
#include <string.h>
#include <symbols.h>
#include <xSIM-api.h>

#define GOP_ARENA_SIZE 0x1100000

static bool prepare_context(struct gop_context *context, uintptr_t *entry)
{
	SIL_CONTEXT sil = {
		.ApobBaseAddress = CONFIG_PSP_APOB_DRAM_ADDRESS,
		.SilMemBaseAddress = (uintptr_t)cbmem_find(CBMEM_ID_AMD_OPENSIL),
	};
	if (!sil.SilMemBaseAddress)
		return false;
	GFXCLASS_INPUT_BLK *gfx = SilFindStructure(&sil, SilId_GfxClass, 0);
	if (!gfx || !gfx->PeiGopEnable || !gfx->PeiGopConfigMemsize ||
	    gfx->PeiGopConfigMemsize > UINT32_MAX || gfx->PeiGopVmFbOffset > UINT32_MAX ||
	    gfx->PeiGopVmFbLocationTop > UINT32_MAX)
		return false;
	printk(BIOS_NOTICE, "AMD GOP: UMA sizeMiB=%llx offset16MiB=%llx top16MiB=%llx\n",
	       gfx->PeiGopConfigMemsize, gfx->PeiGopVmFbOffset, gfx->PeiGopVmFbLocationTop);
	uint64_t uma_base = gfx->PeiGopVmFbOffset << 24;
	uint64_t uma_size = gfx->PeiGopConfigMemsize << 20;
	if ((uma_base + uma_size - 1) >> 24 != gfx->PeiGopVmFbLocationTop)
		return false;

	void *arena = cbmem_add(CBMEM_ID_AMD_PEI_GOP, GOP_ARENA_SIZE);
	uintptr_t base = (uintptr_t)arena;
	if (!arena || base > UINT32_MAX - GOP_ARENA_SIZE || (uintptr_t)_stack > UINT32_MAX ||
	    (uintptr_t)_estack > UINT32_MAX)
		return false;
	*context = (struct gop_context){
		.magic = GOP_CONTEXT_MAGIC,
		.version = GOP_CONTEXT_VERSION,
		.length = sizeof(*context),
		.helper = {base,              0x20000           },
		.stack = {(uintptr_t)_stack, REGION_SIZE(stack)},
		.pool = {base + 0x20000,    0x1000000         },
		.image = {base + 0x1020000,  0x21000           },
		.trace = {base + 0x1041000,  0x10000           },
		.uma_base = uma_base,
		.uma_size = uma_size,
	};
	if (!gop_context_valid(context)) {
		printk(BIOS_ERR, "AMD GOP: context ranges rejected\n");
		return false;
	}
	/* The context is a local object on the existing, linker-owned ramstage stack. */
	uintptr_t object = (uintptr_t)context;
	if (context->stack.size < sizeof(*context) || object < context->stack.base ||
	    object > (uintptr_t)context->stack.base + context->stack.size - sizeof(*context))
		return false;
	size_t length;
	void *blob = cbfs_map("amd/pei_gop", &length);
	if (!blob)
		return false;
	*entry = amd_pei_gop_load_rmodule(blob, length, arena, context->helper.size);
	cbfs_unmap(blob);
	if (!*entry)
		return false;
	memset((void *)(uintptr_t)context->trace.base, 0, context->trace.size);
	printk(BIOS_NOTICE, "AMD GOP: owned helper=%x pool=%x image=%x trace=%x entry=%lx\n",
	       context->helper.base, context->pool.base, context->image.base,
	       context->trace.base, *entry);
	return true;
}

static struct device *graphics_device(void)
{
	struct device *gpu = pcidev_path_behind_pci2pci_bridge(SOC_PCIE_ABC_A_DEV, GFX_DEVFN);
	if (!gpu || !gpu->enabled || gpu->vendor != PCI_VID_ATI ||
	    (gpu->class >> 16) != PCI_BASE_CLASS_DISPLAY)
		return NULL;
	return gpu;
}

static bool prepare_policy(const struct gop_context *context, struct gop_init_policy *policy)
{
	struct device *gpu = graphics_device();
	if (!gpu || !(pci_read_config16(gpu, PCI_COMMAND) & PCI_COMMAND_MEMORY))
		return false;
	struct resource *fb = probe_resource(gpu, PCI_BASE_ADDRESS_0);
	struct resource *mmio = probe_resource(gpu, PCI_BASE_ADDRESS_5);
	if (!fb || !mmio || !fb->size || !mmio->size || fb->base > UINT32_MAX ||
	    fb->size > 0x100000000ULL - fb->base || mmio->base > UINT32_MAX ||
	    mmio->size > 0x100000000ULL - mmio->base)
		return false;
	*policy = (struct gop_init_policy){
		.length = sizeof(*policy),
		.format = 1,
		.content = 1,
		/* Leave the resolution unspecified so GOP selects its preferred mode. */
		.vbios = context->pool.base + context->pool.size - 0x10000,
	};
	for (unsigned int i = 0; i < 6; i++)
		policy->bars[i] = pci_read_config32(gpu, PCI_BASE_ADDRESS_0 + 4 * i);
	if (policy->bars[1] || policy->bars[3])
		return false;

	size_t length;
	void *asset = cbfs_map("amd/atom", &length);
	if (!asset)
		return false;
	const uint8_t *data = asset;
	bool valid = false;
	if (length < 512 || data[0] != 0x55 || data[1] != 0xaa)
		goto done;
	size_t image_size = data[2] * 512U;
	size_t pcir = read_le16(data + 0x18);
	if (image_size < 512 || image_size > length || image_size > 0x10000 ||
	    pcir > image_size - 0x18 || memcmp(data + pcir, "PCIR", 4) ||
	    read_le16(data + pcir + 4) != 0x1002)
		goto done;
	uint8_t *copy = (void *)(uintptr_t)policy->vbios;
	memset(copy, 0, 0x10000);
	memcpy(copy, data, image_size);
	write_le16(copy + pcir + 6, pci_read_config16(gpu, PCI_DEVICE_ID));
	printk(BIOS_NOTICE,
	       "AMD GOP: raw BAR0=%x BAR2=%x BAR4=%x BAR5=%x, mutable ATOM=%x/%zu\n",
	       policy->bars[0], policy->bars[2], policy->bars[4], policy->bars[5],
	       policy->vbios, image_size);
	valid = true;
done:
	cbfs_unmap(asset);
	return valid;
}

static bool publish_graphics(const struct gop_mode *mode, void *vbios, size_t capacity)
{
	struct device *gpu = graphics_device();
	struct resource *fb = gpu ? probe_resource(gpu, PCI_BASE_ADDRESS_0) : NULL;
	printk(BIOS_NOTICE, "AMD GOP: mode %ux%u stride=%u format=%u fb=%llx/%llx\n",
	       mode->width, mode->height, mode->scanline, mode->pixel_format,
	       (unsigned long long)mode->framebuffer,
	       (unsigned long long)mode->framebuffer_size);
	if (!fb || !mode->width || !mode->height || mode->width > 16384 ||
	    mode->height > 16384 || mode->scanline < mode->width || mode->scanline > 32768 ||
	    mode->pixel_format > 1 || !mode->framebuffer_size || mode->framebuffer < fb->base ||
	    mode->framebuffer - fb->base > fb->size ||
	    mode->framebuffer_size > fb->size - (mode->framebuffer - fb->base) ||
	    (uint64_t)mode->scanline * 4 * mode->height > mode->framebuffer_size) {
		printk(BIOS_ERR, "AMD GOP: framebuffer handoff refused\n");
		return false;
	}
	/* Publish the mutable image after GOP has updated the ATOM tables, exactly
	 * as the AMD FSP graphics path does. The existing ACPI builder emits VFCT. */
	struct rom_header *rom = vbios;
	if (!rom || capacity < sizeof(*rom) || rom->signature != PCI_ROM_HDR || !rom->size)
		return false;
	size_t rom_size = rom->size * 512U;
	if (rom_size > capacity || rom_size < sizeof(struct pci_data) ||
	    rom->data > rom_size - sizeof(struct pci_data))
		return false;
	struct pci_data *pcir = (void *)((uint8_t *)rom + rom->data);
	if (pcir->signature != PCI_DATA_HDR || pcir->dlen < sizeof(*pcir) ||
	    pcir->dlen > rom_size - rom->data || pcir->vendor != gpu->vendor ||
	    pcir->device != gpu->device || pcir->ilen * 512U != rom_size)
		return false;
	uint8_t *bytes = vbios;
	bytes[VFCT_VBIOS_CHECKSUM_OFFSET] = 0;
	bytes[VFCT_VBIOS_CHECKSUM_OFFSET] = acpi_checksum(bytes, rom_size);
	gpu->pci_vga_option_rom = rom;

	struct lb_framebuffer info = {
		.physical_address = mode->framebuffer,
		.x_resolution = mode->width,
		.y_resolution = mode->height,
		.bytes_per_line = mode->scanline * 4,
		.bits_per_pixel = 32,
		.red_mask_pos = mode->pixel_format ? 16 : 0,
		.red_mask_size = 8,
		.green_mask_pos = 8,
		.green_mask_size = 8,
		.blue_mask_pos = mode->pixel_format ? 0 : 16,
		.blue_mask_size = 8,
		.reserved_mask_pos = 24,
		.reserved_mask_size = 8,
	};
	return fb_add_framebuffer_info_ex(&info) != NULL;
}

static void initialize_graphics(void *unused)
{
	struct gop_context context;
	struct gop_init_policy policy;
	struct gop_mode mode = {0};
	uintptr_t entry;

	if (!prepare_context(&context, &entry) || !prepare_policy(&context, &policy)) {
		printk(BIOS_ERR, "AMD GOP: preparation failed\n");
		return;
	}
	unsigned int status = protected_mode_call_wrapper(entry, (uintptr_t)&context,
							  (uintptr_t)&policy, (uintptr_t)&mode);
	uintptr_t base = (uintptr_t)cbmem_find(CBMEM_ID_AMD_PEI_GOP);
	if (base && context.trace.base == base + 0x1041000 && context.trace.size == 0x10000 &&
	    context.trace_used <= context.trace.size)
		printk(BIOS_DEBUG, "%.*s", (int)context.trace_used,
		       (const char *)(uintptr_t)context.trace.base);
	if (status ||
	    !publish_graphics(
		    &mode, (void *)(uintptr_t)(context.pool.base + context.pool.size - 0x10000),
		    0x10000))
		printk(BIOS_ERR, "AMD GOP: initialization/handoff failed, status=%#x\n",
		       status);
}

BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, initialize_graphics, NULL);
