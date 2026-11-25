/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <cpu/x86/smm.h>
#include <drivers/efi/capsule_regions.h>
#include <efi_variable_service.h>
#include <fmap.h>
#if !CONFIG(SOC_AMD_PHOENIX_OPENSIL)
#include <intelblocks/fast_spi.h>
#endif
#include <payload_mm_cms.h>
#include <string.h>

#include "hob.h"
#include "native_capsule.h"
#include "native_smm.h"

extern const uint8_t ass_capsule_trust_start[], ass_capsule_trust_end[];

static uintptr_t ram_end;

static bool read_flash(size_t offset, void *data, size_t size)
{
	const struct region_device *flash = CONFIG(SOC_AMD_PHOENIX_OPENSIL) ?
		efi_variable_service_read_device() : boot_device_rw();

	/* Use the same SPI view as the writer, not cached mapped ROM. */
	return flash && rdev_readat(flash, data, offset, size) == (ssize_t)size;
}

static const void *map_capsule(uint64_t address, size_t size)
{
	uintptr_t mailbox;
	size_t mailbox_size;

	smm_get_efi_variable_service_buffer(&mailbox, &mailbox_size);
	if (!ass_capsule_source_in_ram(address, size, 1 * MiB, ram_end, mailbox,
	    mailbox_size) || smm_points_to_smram((const void *)(uintptr_t)address, size))
		return NULL;
	return (const void *)(uintptr_t)address;
}

const struct ass_capsule_platform *ass_native_smm_capsule_platform(
	const struct efi_variable_service_state *state, ass_capsule_program_fn *program,
	uint32_t boot_slot, uint32_t (*commit)(uint32_t))
{
	static struct ass_capsule_platform platform;
	static struct payload_mm_crypto_span trust;
	static char names[ASS_CAPSULE_MAX_RANGES][16];
	static struct ass_capsule_range ranges[ARRAY_SIZE(names)];
	const char *input = efi_capsule_region_names();
	size_t count = 0;

	if (!state || !program || boot_slot > 3 || (boot_slot && !commit) ||
	    state->capsule_ram_end <= 1 * MiB ||
	    state->capsule_info < state->hob_base ||
	    state->capsule_info - state->hob_base > state->hob_size ||
	    state->hob_size - (state->capsule_info - state->hob_base) <
		sizeof(struct capsule_info_hob))
		return NULL;
	const struct capsule_info_hob *info = (const void *)(uintptr_t)state->capsule_info;
	if (info->flash_size != CONFIG_ROM_SIZE || info->block_size != EFI_CAPSULE_BLOCK_SIZE)
		return NULL;
	memset(names, 0, sizeof(names));
	while (*input) {
		struct region area;
		size_t length = 0;

		while (*input == ' ' || *input == '\t')
			input++;
		if (!*input)
			break;
		if (count == ARRAY_SIZE(names))
			return NULL;
		while (*input && *input != ' ' && *input != '\t') {
			if (length == sizeof(names[0]) - 1 || *input <= ' ' || *input > '~')
				return NULL;
			names[count][length++] = *input++;
		}
		if (fmap_locate_area(names[count], &area) || !region_sz(&area) ||
		    region_offset(&area) >= CONFIG_ROM_SIZE ||
		    region_sz(&area) > CONFIG_ROM_SIZE - region_offset(&area))
			return NULL;
		for (size_t i = 0; i < count; i++)
			if (!strcmp(names[i], names[count]))
				return NULL;
		ranges[count++] = (struct ass_capsule_range) {
			region_offset(&area), region_sz(&area),
		};
	}
	if (!count)
		return NULL;
	size_t bios_size, bios_offset;

#if CONFIG(SOC_AMD_PHOENIX_OPENSIL)
	const struct region bios = efi_variable_service_bios_region();

	bios_offset = region_offset(&bios);
	bios_size = region_sz(&bios);
#else
	bios_offset = fast_spi_get_bios_region(&bios_size);
#endif
	if (!bios_size || bios_offset >= CONFIG_ROM_SIZE || bios_size > CONFIG_ROM_SIZE - bios_offset)
		return NULL;
	trust = (struct payload_mm_crypto_span) {
		.data = ass_capsule_trust_start,
		.size = (uintptr_t)ass_capsule_trust_end - (uintptr_t)ass_capsule_trust_start,
	};
	platform = (struct ass_capsule_platform) {
		.geometry = {
			.flash_size = info->flash_size, .block_size = info->block_size,
			.compiled_floor = info->lowest_supported_version,
			.read_flash = read_flash,
		},
		.fmap = { FMAP_SECTION_FMAP_START, FMAP_SECTION_FMAP_SIZE },
		.store = { FMAP_SECTION_SMMSTORE_START, FMAP_SECTION_SMMSTORE_SIZE },
		.bios = { bios_offset, bios_size },
		.names = names, .ranges = ranges, .count = count,
		.root = &trust, .map_source = map_capsule, .program = program,
		.boot_slot = boot_slot, .commit = commit,
	};
	memcpy(platform.geometry.image_guid, info->image_guid, sizeof(info->image_guid));
	ram_end = state->capsule_ram_end;
	return &platform;
}
