/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * coreboot platform adapter for the native cdk2 stage.
 */

#include <library/cdk2_native_services.h>
#include <guid/acpi_board_info.h>
#include <guid/cbmem_table_hob.h>
#include <guid/cfr_setup_menu.h>
#include <guid/deadline_tsc_info.h>
#include <guid/firmware_info.h>
#include <guid/graphics_info_hob.h>
#include <guid/serial_port_info.h>
#include <cdk2/diagnostic.h>
#include <cdk2/acpi_board_info.h>
#include <cdk2/direct_image_table.h>
#include <cdk2/dma_handoff.h>
#include <cdk2/early_splash.h>
#include <cdk2/linear_boot.h>
#include <cdk2/payload_mm_authvar_service.h>
#include <guid/smmstore_info.h>
#include <guid/smram_memory.h>
#include <guid/smm_register_info.h>
#include <guid/local_apic_timer_info.h>
#include <guid/tsc_info.h>
#include <industry_standard/acpi.h>
#include <industry_standard/mcfg.h>
#include <industry_standard/tpm20.h>
#include <industry_standard/tpm2_acpi.h>
#include <industry_standard/uefi_tcg_platform.h>
#include <guid/tcg_physical_presence.h>
#include <guid/variable_flash_info.h>
#include <cdk2/hob_payload.h>
#include <cdk2/config.h>

#include "coreboot_hobs.h"
#include "pe.h"
#include "symbols.h"

#define CDK2_ACPI_MCFG_ALLOCATION \
	EFI_ACPI_MEMORY_MAPPED_ENHANCED_CONFIGURATION_SPACE_BASE_ADDRESS_ALLOCATION_STRUCTURE

#define CDK2_COREBOOT_HOB_REGION_SIZE (0x04000000U)
#define CDK2_COREBOOT_DXE_MAX_PAGES   (0x2000U)

#define CDK2_COREBOOT_MAX_TPM2_LOG_SIZE  (1024U * 1024U)
#define CDK2_COREBOOT_MAX_TPM_PCR_INDEX  23U
#define CDK2_COREBOOT_SPEC_ID_EVENT_NAME "Spec ID Event"
#define CDK2_COREBOOT_CFR_MAX_DEPTH      32U
#define CDK2_COREBOOT_CFR_OPTION_FLAGS_MASK                                   \
	(CFR_OPTFLAG_READONLY | CFR_OPTFLAG_INACTIVE | CFR_OPTFLAG_SUPPRESS | \
	 CFR_OPTFLAG_VOLATILE | CFR_OPTFLAG_RUNTIME)

#define CDK2_COREBOOT_8259_COMMAND_REGISTER_MASTER 0x20U
#define CDK2_COREBOOT_8259_MASK_REGISTER_MASTER    0x21U
#define CDK2_COREBOOT_8259_COMMAND_REGISTER_SLAVE  0xA0U
#define CDK2_COREBOOT_8259_MASK_REGISTER_SLAVE     0xA1U
#define CDK2_COREBOOT_8259_EOI                     0x20U
#define CDK2_COREBOOT_UEFI_MXCSR                   0x1f80U
#define CDK2_COREBOOT_SMBIOS_ENTRY_ALIGNMENT       16U
#define CDK2_COREBOOT_SMBIOS2_ENTRY_SIZE           31U
#define CDK2_COREBOOT_SMBIOS3_ENTRY_SIZE           24U
#define CDK2_COREBOOT_SMBIOS2_INTERMEDIATE_OFFSET  16U
#define CDK2_COREBOOT_SMBIOS2_TABLE_LENGTH_OFFSET  22U
#define CDK2_COREBOOT_SMBIOS2_TABLE_ADDRESS_OFFSET 24U
#define CDK2_COREBOOT_SMBIOS3_TABLE_LENGTH_OFFSET  12U
#define CDK2_COREBOOT_SMBIOS3_TABLE_ADDRESS_OFFSET 16U
#define CDK2_COREBOOT_ACPI_RSDP_V1_SIZE \
	OFFSET_OF(EFI_ACPI_3_0_ROOT_SYSTEM_DESCRIPTION_POINTER, length)
#define CDK2_COREBOOT_ACPI_RSDP_MAX_SIZE 4096U

_Static_assert(sizeof(struct cdk2_deadline_tsc_info) == 16U,
	"deadline TSC-info ABI size changed");
_Static_assert(OFFSET_OF(struct cdk2_deadline_tsc_info, frequency_hz) == 8U,
	"deadline TSC-info frequency is not naturally aligned");
_Static_assert(sizeof(struct cb_boot_splash) == 48U,
	"coreboot splash ABI size changed");
_Static_assert(OFFSET_OF(struct cb_boot_splash, framebuffer_address) == 12U,
	"coreboot splash framebuffer address moved");
_Static_assert(OFFSET_OF(struct cb_boot_splash, bmp_address) == 36U,
	"coreboot splash BMP address moved");

#if defined(__GNUC__)
#define CDK2_COREBOOT_NORETURN __noreturn
#else
#define CDK2_COREBOOT_NORETURN
#endif

static struct cdk2_coreboot_handoff m_coreboot_handoff;
#if CONFIG_PAYLOAD_DMA_HANDOFF
static struct cdk2_dma_handoff_state m_dma_handoff;
#endif
#if CONFIG_CDK2_LINEAR_BOOT
static struct cdk2_linear_state m_linear_state;
#endif
static const UINT8 m_cdk2_smbios2_anchor[] = {'_', 'S', 'M', '_'};
static const UINT8 m_cdk2_smbios3_anchor[] = {'_', 'S', 'M', '3', '_'};
static const UINT8 m_cdk2_smbios2_intermediate_anchor[] = {'_', 'D', 'M', 'I', '_'};

static const EFI_GUID m_cdk2_graphics_info_hob_guid = {
	0x39f62cce,
	0x6825,
	0x4669,
	{0xbb, 0x56, 0x54, 0x1a, 0xba, 0x75, 0x3a, 0x07}
};
static const EFI_GUID m_cdk2_smm_store_info_hob_guid = {
	0xf585ca19,
	0x881b,
	0x44fb,
	{0x3f, 0x3d, 0x81, 0x89, 0x7c, 0x57, 0xbb, 0x01}
};
static const EFI_GUID m_cdk2_variable_flash_info_hob_guid = {
	0x5d11c653,
	0x8154,
	0x4ac3,
	{0xa8, 0xc2, 0xfb, 0xa2, 0x89, 0x20, 0xfc, 0x90}
};
static const EFI_GUID m_cdk2_smram_memory_guid = {
	0x6dadf1d1,
	0xd4cc,
	0x4910,
	{0xbb, 0x6e, 0x82, 0xb1, 0xfd, 0x80, 0xff, 0x3d}
};
static const EFI_GUID m_cdk2_smm_register_info_guid = {
	0xaa9bd7a7,
	0xcafb,
	0x4499,
	{0xa4, 0xa9, 0x0b, 0x34, 0x6b, 0x40, 0xa6, 0x22}
};
static const EFI_GUID m_cdk2_s3_communication_guid = {
	0x88e31ba1,
	0x1856,
	0x4b8b,
	{0xbb, 0xdf, 0xf8, 0x16, 0xdd, 0x94, 0x0a, 0xef}
};

static const EFI_GUID m_cdk2_local_apic_timer_info_guid = {
	0x18c7a13d, 0x9a71, 0x4f5d, { 0x91, 0x68, 0x44, 0x7b, 0x69, 0x8c, 0x12, 0x56 }
};
static const EFI_GUID m_cdk2_tsc_info_guid = {
	0x94d39041, 0x65eb, 0x4126, { 0x9d, 0x53, 0x57, 0x16, 0xcc, 0x5d, 0x03, 0xfb }
};
static const EFI_GUID m_cdk2_deadline_tsc_info_guid =
	CDK2_DEADLINE_TSC_INFO_GUID;
static const EFI_GUID m_cdk2_firmware_info_hob_guid =
	CDK2_FIRMWARE_INFO_HOB_GUID;
static const EFI_GUID m_cdk2_tcg_physical_presence_info_hob_guid = {
	0xf367be59,
	0x5891,
	0x40eb,
	{0x21, 0x44, 0xed, 0x2e, 0xac, 0x57, 0xfd, 0x14}
};
static const EFI_GUID m_cdk2_acpi_board_info_hob_guid = {
	0x0ad3d31b,
	0xb3d8,
	0x4506,
	{0xae, 0x71, 0x2e, 0xf1, 0x10, 0x06, 0xd9, 0x0f}
};
static const EFI_GUID m_cdk2_serial_port_info_guid = {
	0xaa7e190d,
	0xbe21,
	0x4409,
	{0x8e, 0x67, 0xa2, 0xcd, 0x0f, 0x61, 0xe1, 0x70}
};
static const EFI_GUID m_cdk2_legacy_serial_port_info_guid = {
	0x6c6872fe,
	0x56a9,
	0x4403,
	{0xbb, 0x98, 0x95, 0x8d, 0x62, 0xde, 0x87, 0xf1}
};

#if CONFIG_CDK2_CBMEM_TIMESTAMPS || CONFIG_CDK2_LINEAR_BOOT
static uint64_t cdk2_diagnostic_clock(void *context)
{
	UINT32 low, high;

	(void)context;
	__asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
	return (UINT64)low | ((UINT64)high << 32);
}
#endif

static EFI_STATUS cdk2_coreboot_append_tsc_info_hob(
	EFI_HOB_HANDOFF_INFO_TABLE *handoff,
	const struct cdk2_coreboot_handoff *coreboot)
{
	const struct cb_tsc_info *record;
	struct cdk2_tsc_info info;
	const void *raw;
	EFI_STATUS status;

	status = cdk2_coreboot_find_unique_record(coreboot, CB_TAG_TSC_INFO,
		CDK2_COREBOOT_TSC_INFO_MIN_SIZE, &raw);
	if (EFI_ERROR(status))
		return status;
	record = raw;
	if (record->freq_khz == 0U)
		return EFI_COMPROMISED_DATA;
	info = (struct cdk2_tsc_info) {
		.revision = CDK2_TSC_INFO_REVISION,
		.frequency_hz = (UINT64)record->freq_khz * 1000U,
	};
	return cdk2_coreboot_append_guid_hob(handoff, &m_cdk2_tsc_info_guid,
		&info, sizeof(info));
}

#if CONFIG_CDK2_SERIAL
struct cdk2_diagnostic_mmio_uart {
	UINTN base;
	UINT32 stride;
	UINT32 width;
};

static void cdk2_diagnostic_io_putc(char character, void *context)
{
	const struct cdk2_diagnostic_mmio_uart *uart = context;
	UINT16 base = (UINT16)uart->base;
	UINTN timeout = 100000U;
	UINT8 ready;

	do {
		__asm__ volatile ("inb %w1, %0" : "=a"(ready) :
			"Nd"((UINT16)(base + 5U * uart->stride)));
		if ((ready & 0x20U) != 0U)
			break;
		if (timeout == 0U)
			return;
		timeout--;
	} while (TRUE);
	__asm__ volatile ("outb %0, %w1" : : "a"((UINT8)character), "Nd"(base));
}

static struct cdk2_diagnostic_mmio_uart m_diagnostic_mmio_uart;
#if CONFIG_CDK2_SPI_CONSOLE
/* Native diagnostics run before the handoff HOB exists. Carry their consumed
 * budget into that HOB so every later PE observes the same boot-wide limit. */
static volatile UINT32 m_diagnostic_spi_console_remaining;
static BOOLEAN m_diagnostic_spi_console_active;
#endif

static UINT32 cdk2_diagnostic_mmio_read(const struct cdk2_diagnostic_mmio_uart *uart,
	UINTN reg)
{
	UINTN address = uart->base + reg * uart->stride;

	if (uart->width == 4U)
		return *(volatile uint32_t *)address;
	if (uart->width == 2U)
		return *(volatile uint16_t *)address;
	return *(volatile UINT8 *)address;
}

static void cdk2_diagnostic_mmio_write(const struct cdk2_diagnostic_mmio_uart *uart,
	UINTN reg, UINT8 value)
{
	UINTN address = uart->base + reg * uart->stride;

	if (uart->width == 4U)
		*(volatile uint32_t *)address = value;
	else if (uart->width == 2U)
		*(volatile uint16_t *)address = value;
	else
		*(volatile UINT8 *)address = value;
}

static void cdk2_diagnostic_mmio_putc(char character, void *context)
{
	const struct cdk2_diagnostic_mmio_uart *uart = context;
	UINTN timeout = 100000U;

	while ((cdk2_diagnostic_mmio_read(uart, 5U) & 0x20U) == 0U) {
		if (timeout-- == 0U)
			return;
	}
	cdk2_diagnostic_mmio_write(uart, 0U, (UINT8)character);
}
#endif
static const EFI_GUID m_cdk2_acpi_table_guid = {
	0x9f9a9506,
	0x5597,
	0x4515,
	{0xba, 0xb6, 0x8b, 0xcd, 0xe7, 0x84, 0xba, 0x87}
};
static const EFI_GUID m_cdk2_smbios_table_guid = {
	0x590a0d26,
	0x06e5,
	0x4d20,
	{0x8a, 0x82, 0x59, 0xea, 0x1b, 0x34, 0x98, 0x2d}
};
static const EFI_GUID m_cdk2_smbios3_table_guid = {
	0x92b7896c,
	0x3362,
	0x46ce,
	{0x99, 0xb3, 0x4f, 0x5e, 0x3c, 0x34, 0xeb, 0x42}
};
static const EFI_GUID m_cdk2_coreboot_table_guid = {
	0x9e0d4b6f,
	0xa8e8,
	0x4d7e,
	{0x9a, 0x2d, 0x31, 0x0d, 0x4c, 0x9a, 0x8f, 0x2b}
};
static const EFI_GUID m_cdk2_cfr_setup_menu_form_guid = {
	0xfbc3b1de,
	0xd17c,
	0x44de,
	{0x98, 0x47, 0x2b, 0xbf, 0x9e, 0xfd, 0xbd, 0x8e}
};
static const EFI_GUID m_cdk2_tcg_event2_entry_hob_guid = {
	0xd26c221e,
	0x2430,
	0x4c8a,
	{0x91, 0x70, 0x3f, 0xcb, 0x45, 0x00, 0x41, 0x3f}
};

#define CDK2_COREBOOT_MAX_ACPI_TABLE_SIZE (1024U * 1024U)

static void cdk2_coreboot_copy_bytes(void *destination, const void *source, UINTN length);
static EFI_STATUS cdk2_coreboot_find_cbmem_entry(
	const struct cdk2_coreboot_handoff *coreboot, UINT32 id, UINT32 minimum_size,
	EFI_PHYSICAL_ADDRESS *base, UINT32 *size);

static UINT16 cdk2_coreboot_read16(const void *source)
{
	UINT16 value;

	cdk2_coreboot_copy_bytes(&value, source, sizeof(value));
	return value;
}

static UINT32 cdk2_coreboot_read32(const void *source)
{
	UINT32 value;

	cdk2_coreboot_copy_bytes(&value, source, sizeof(value));
	return value;
}

static UINT64 cdk2_coreboot_read64(const void *source)
{
	UINT64 value;

	cdk2_coreboot_copy_bytes(&value, source, sizeof(value));
	return value;
}

static UINT32 cdk2_coreboot_acpi_read32(const void *source)
{
	return cdk2_coreboot_read32(source);
}

static UINT64 cdk2_coreboot_acpi_read64(const void *source)
{
	return cdk2_coreboot_read64(source);
}

static BOOLEAN cdk2_coreboot_bytes_equal(const void *left, const void *right, UINTN length)
{
	const UINT8 *left_bytes;
	const UINT8 *right_bytes;
	UINTN index;

	if (left == NULL || right == NULL) {
		return FALSE;
	}

	left_bytes = (const UINT8 *)left;
	right_bytes = (const UINT8 *)right;
	for (index = 0; index < length; index++) {
		if (left_bytes[index] != right_bytes[index]) {
			return FALSE;
		}
	}

	return TRUE;
}

static UINT8 cdk2_coreboot_checksum8(const void *buffer, UINTN length)
{
	const UINT8 *bytes;
	UINT8 checksum;
	UINTN index;

	bytes = (const UINT8 *)buffer;
	checksum = 0;
	for (index = 0; index < length; index++) {
		checksum = (UINT8)(checksum + bytes[index]);
	}

	return checksum;
}

static UINT32 cdk2_coreboot_crc32(const void *buffer, UINTN length)
{
	const UINT8 *bytes;
	UINT32 crc;
	UINTN index;
	UINTN bit_index;

	if (buffer == NULL) {
		return 0;
	}

	bytes = (const UINT8 *)buffer;
	crc = 0;
	for (index = 0; index < length; index++) {
		crc ^= (UINT32)bytes[index] << 24;
		for (bit_index = 0; bit_index < 8; bit_index++) {
			if ((crc & BIT31) != 0) {
				crc = (crc << 1) ^ 0x04C11DB7U;
			} else {
				crc <<= 1;
			}
		}
	}

	return crc;
}

static BOOLEAN cdk2_coreboot_acpi_field_present(const EFI_ACPI_DESCRIPTION_HEADER *table,
						UINTN offset, UINTN size)
{
	return table != NULL && table->length >= offset && size <= table->length - offset;
}

static EFI_STATUS cdk2_coreboot_acpi_inspect_table(
	EFI_PHYSICAL_ADDRESS address, EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE **fadt,
	EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER **mcfg,
	BOOLEAN *tpm2_present, BOOLEAN *tcpa_present, EFI_ACPI_DESCRIPTION_HEADER **tpm2_table,
	EFI_ACPI_DESCRIPTION_HEADER **tcpa_table)
{
	EFI_ACPI_DESCRIPTION_HEADER *header;

	if (address == 0 || fadt == NULL || mcfg == NULL || tpm2_present == NULL ||
	    tcpa_present == NULL || tpm2_table == NULL || tcpa_table == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	header = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)address;
	if (header->length < sizeof(*header) ||
	    header->length > CDK2_COREBOOT_MAX_ACPI_TABLE_SIZE) {
		return EFI_COMPROMISED_DATA;
	}

	/*
	 * Only consume and validate tables used to construct payload HOBs.  Firmware
	 * may publish dynamically patched AML tables whose checksum is not useful to
	 * this early handoff; rejecting an unrelated table would prevent DXE from
	 * receiving the otherwise valid ACPI set.
	 */
	switch (header->signature) {
	case EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE:
	case EFI_ACPI_6_6_PCI_EXPRESS_MEMORY_MAPPED_CONFIGURATION_SPACE_BASE_ADDRESS_DESCRIPTION_TABLE_SIGNATURE:
	case EFI_ACPI_5_0_TRUSTED_COMPUTING_PLATFORM_2_TABLE_SIGNATURE:
	case EFI_ACPI_5_0_TRUSTED_COMPUTING_PLATFORM_ALLIANCE_CAPABILITIES_TABLE_SIGNATURE:
		break;
	default:
		return EFI_SUCCESS;
	}

	if (cdk2_coreboot_checksum8(header, header->length) != 0) {
		return EFI_COMPROMISED_DATA;
	}

	switch (header->signature) {
	case EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE:
		if (!cdk2_coreboot_acpi_field_present(
			    header,
			    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, gpe0_blk_len),
			    sizeof(((EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE *)0)
					   ->gpe0_blk_len))) {
			return EFI_COMPROMISED_DATA;
		}
		if (*fadt != NULL) {
			return *fadt == (EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE *)header ?
				EFI_SUCCESS : EFI_COMPROMISED_DATA;
		}

		*fadt = (EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE *)header;
		break;

	case EFI_ACPI_6_6_PCI_EXPRESS_MEMORY_MAPPED_CONFIGURATION_SPACE_BASE_ADDRESS_DESCRIPTION_TABLE_SIGNATURE:
		if (header->length <
			    sizeof(EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER) +
				    sizeof(CDK2_ACPI_MCFG_ALLOCATION) ||
		    ((header->length -
		      sizeof(EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER)) %
		     sizeof(CDK2_ACPI_MCFG_ALLOCATION)) != 0) {
			return EFI_COMPROMISED_DATA;
		}
		if (*mcfg != NULL) {
			return *mcfg ==
				(EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER *)header ?
				EFI_SUCCESS : EFI_COMPROMISED_DATA;
		}

		*mcfg = (EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER *)header;
		break;

	case EFI_ACPI_5_0_TRUSTED_COMPUTING_PLATFORM_2_TABLE_SIGNATURE:
		if (*tpm2_present)
			return *tpm2_table == header ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
		*tpm2_present = TRUE;
		*tpm2_table = header;

		break;

	case EFI_ACPI_5_0_TRUSTED_COMPUTING_PLATFORM_ALLIANCE_CAPABILITIES_TABLE_SIGNATURE:
		if (*tcpa_present)
			return *tcpa_table == header ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
		*tcpa_present = TRUE;
		*tcpa_table = header;
		break;

	default:
		break;
	}

	return EFI_SUCCESS;
}

struct cdk2_coreboot_acpi_root_result {
	EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE *fadt;
	EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER *mcfg;
	EFI_ACPI_DESCRIPTION_HEADER *tpm2_table;
	EFI_ACPI_DESCRIPTION_HEADER *tcpa_table;
	BOOLEAN tpm2_present;
	BOOLEAN tcpa_present;
};

static EFI_STATUS cdk2_coreboot_acpi_inspect_root(
	EFI_PHYSICAL_ADDRESS address, BOOLEAN extended,
	EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE **fadt,
	EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER **mcfg,
	BOOLEAN *tpm2_present, BOOLEAN *tcpa_present,
	EFI_ACPI_DESCRIPTION_HEADER **tpm2_table,
	EFI_ACPI_DESCRIPTION_HEADER **tcpa_table);

static EFI_STATUS cdk2_coreboot_acpi_parse_root(EFI_PHYSICAL_ADDRESS address,
	BOOLEAN extended, struct cdk2_coreboot_acpi_root_result *result)
{
	if (result == NULL)
		return EFI_INVALID_PARAMETER;
	*result = (struct cdk2_coreboot_acpi_root_result){0};
	return cdk2_coreboot_acpi_inspect_root(address, extended, &result->fadt,
		&result->mcfg, &result->tpm2_present, &result->tcpa_present,
		&result->tpm2_table, &result->tcpa_table);
}

static BOOLEAN cdk2_coreboot_acpi_legacy_agrees(
	const struct cdk2_coreboot_acpi_root_result *legacy,
	const struct cdk2_coreboot_acpi_root_result *extended)
{
	#define LEGACY_POINTER_AGREES(member) \
		((UINTN)extended->member > MAX_UINT32 ? legacy->member == NULL : \
		 legacy->member == extended->member)

	return LEGACY_POINTER_AGREES(fadt) && LEGACY_POINTER_AGREES(mcfg) &&
		LEGACY_POINTER_AGREES(tpm2_table) && LEGACY_POINTER_AGREES(tcpa_table);
	#undef LEGACY_POINTER_AGREES
}

static EFI_STATUS cdk2_coreboot_acpi_inspect_root(
	EFI_PHYSICAL_ADDRESS address, BOOLEAN extended,
	EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE **fadt,
	EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER **mcfg,
	BOOLEAN *tpm2_present, BOOLEAN *tcpa_present, EFI_ACPI_DESCRIPTION_HEADER **tpm2_table,
	EFI_ACPI_DESCRIPTION_HEADER **tcpa_table)
{
	EFI_ACPI_DESCRIPTION_HEADER *header;
	UINTN entry_count;
	UINTN entry_size;
	UINTN index;
	UINT32 root_signature;
	UINT64 table_address;
	EFI_STATUS status;

	if (address == 0 || fadt == NULL || mcfg == NULL || tpm2_present == NULL ||
	    tcpa_present == NULL || tpm2_table == NULL || tcpa_table == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	header = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)address;
	entry_size = extended ? sizeof(UINT64) : sizeof(UINT32);
	root_signature = extended ? EFI_ACPI_3_0_EXTENDED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE :
				    EFI_ACPI_3_0_ROOT_SYSTEM_DESCRIPTION_TABLE_SIGNATURE;
	if (header->signature != root_signature || header->length < sizeof(*header) ||
	    header->length > CDK2_COREBOOT_MAX_ACPI_TABLE_SIZE ||
	    ((header->length - sizeof(*header)) % entry_size) != 0) {
		return EFI_COMPROMISED_DATA;
	}

	if (cdk2_coreboot_checksum8(header, header->length) != 0) {
		return EFI_COMPROMISED_DATA;
	}

	entry_count = (header->length - sizeof(*header)) / entry_size;
	for (index = 0; index < entry_count; index++) {
		if (extended) {
			table_address = cdk2_coreboot_acpi_read64((UINT8 *)(header + 1) +
								 index * entry_size);
		} else {
			table_address = cdk2_coreboot_acpi_read32((UINT8 *)(header + 1) +
								 index * entry_size);
		}

		status = cdk2_coreboot_acpi_inspect_table(table_address, fadt, mcfg, tpm2_present,
							  tcpa_present, tpm2_table, tcpa_table);
		if (EFI_ERROR(status)) {
			return status;
		}
	}

	return EFI_SUCCESS;
}

static EFI_STATUS
cdk2_coreboot_validate_rsdp(const EFI_ACPI_3_0_ROOT_SYSTEM_DESCRIPTION_POINTER *rsdp,
			    BOOLEAN *extended)
{
	if (rsdp == NULL || extended == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	if (rsdp->signature != EFI_ACPI_3_0_ROOT_SYSTEM_DESCRIPTION_POINTER_SIGNATURE ||
	    cdk2_coreboot_checksum8(rsdp, CDK2_COREBOOT_ACPI_RSDP_V1_SIZE) != 0) {
		return EFI_COMPROMISED_DATA;
	}

	*extended = FALSE;
	if (rsdp->revision == 0) {
		return EFI_SUCCESS;
	}

	if (rsdp->length < sizeof(*rsdp) || rsdp->length > CDK2_COREBOOT_ACPI_RSDP_MAX_SIZE ||
	    cdk2_coreboot_checksum8(rsdp, rsdp->length) != 0) {
		return EFI_COMPROMISED_DATA;
	}

	*extended = TRUE;
	return EFI_SUCCESS;
}

static BOOLEAN cdk2_coreboot_acpi_read_integer(const UINT8 **aml, const UINT8 *end,
					       UINT64 *value)
{
	UINTN bytes;
	UINTN index;

	if (*aml >= end || value == NULL) {
		return FALSE;
	}
	if (**aml == 0x00 || **aml == 0x01) {
		*value = *(*aml)++;
		return TRUE;
	}
	switch (*(*aml)++) {
	case 0x0a:
		bytes = 1;
		break;
	case 0x0b:
		bytes = 2;
		break;
	case 0x0c:
		bytes = 4;
		break;
	case 0x0e:
		bytes = 8;
		break;
	default:
		return FALSE;
	}
	if ((UINTN)(end - *aml) < bytes) {
		return FALSE;
	}
	*value = 0;
	for (index = 0; index < bytes; index++) {
		*value |= (UINT64)(*aml)[index] << (index * 8);
	}
	*aml += bytes;
	return TRUE;
}

static BOOLEAN cdk2_coreboot_aml_package(const UINT8 *aml, const UINT8 *end,
	const UINT8 **body, const UINT8 **package_end)
{
	UINTN extra;
	UINTN encoded;
	UINTN length;
	UINTN index;

	if (aml >= end || body == NULL || package_end == NULL)
		return FALSE;
	extra = *aml >> 6;
	encoded = extra + 1U;
	if ((UINTN)(end - aml) < encoded)
		return FALSE;
	length = extra == 0U ? *aml & 0x3fU : *aml & 0x0fU;
	for (index = 0; index < extra; index++)
		length |= (UINTN)aml[index + 1U] << (4U + index * 8U);
	if (length < encoded || (UINTN)(end - aml) < length)
		return FALSE;
	*body = aml + encoded;
	*package_end = aml + length;
	return TRUE;
}

static const UINT8 *cdk2_coreboot_aml_skip_name(const UINT8 *aml, const UINT8 *end,
	const UINT8 **last_segment)
{
	UINTN segments = 1U;

	while (aml < end && (*aml == '\\' || *aml == '^'))
		aml++;
	if (aml >= end)
		return NULL;
	if (*aml == 0x00)
		return aml + 1U;
	if (*aml == 0x2e) {
		segments = 2U;
		aml++;
	} else if (*aml == 0x2f) {
		if (++aml >= end)
			return NULL;
		segments = *aml++;
		if (segments == 0U)
			return NULL;
	}
	if ((UINTN)(end - aml) < segments * 4U)
		return NULL;
	if (last_segment != NULL)
		*last_segment = aml + (segments - 1U) * 4U;
	return aml + segments * 4U;
}

static const UINT8 *cdk2_coreboot_aml_skip_termarg(const UINT8 *aml,
	const UINT8 *end)
{
	const UINT8 *next = aml;
	UINT64 value;

	if (cdk2_coreboot_acpi_read_integer(&next, end, &value))
		return next;
	return cdk2_coreboot_aml_skip_name(aml, end, NULL);
}

static UINT8 parsed_s5_sleep_type_b;

static BOOLEAN cdk2_coreboot_aml_find_s5(const UINT8 *aml, const UINT8 *end,
	UINT8 *sleep_type, UINTN depth)
{
	if (depth > 64U)
		return FALSE;
	while (aml < end) {
		const UINT8 *body;
		const UINT8 *object_end;
		const UINT8 *name;
		const UINT8 *next;
		UINT64 value;

		if (*aml == 0x08) { /* NameOp */
			const UINT8 *name_start = aml + 1U;

			next = cdk2_coreboot_aml_skip_name(name_start, end, &name);
			if (next == NULL)
				return FALSE;
			if (depth == 0U &&
			    ((UINTN)(next - name_start) == 4U ||
			     ((UINTN)(next - name_start) == 5U && *name_start == '\\')) &&
			    (UINTN)(end - next) >= 2U && name[0] == '_' && name[1] == 'S' &&
			    name[2] == '5' && name[3] == '_' && *next == 0x12 &&
			    cdk2_coreboot_aml_package(next + 1U, end, &body, &object_end) &&
			    body < object_end && *body++ >= 2U &&
			    cdk2_coreboot_acpi_read_integer(&body, object_end, &value) &&
			    value <= 7U) {
				*sleep_type = (UINT8)value;
				parsed_s5_sleep_type_b = *sleep_type;
				if (cdk2_coreboot_acpi_read_integer(&body, object_end,
				    &value) && value <= 7U)
					parsed_s5_sleep_type_b = (UINT8)value;
				return TRUE;
			}
			aml = next;
			continue;
		}
		if (*aml == 0x0d) { /* StringPrefix */
			aml++;
			while (aml < end && *aml++ != 0U)
				;
			continue;
		}
		if (*aml == 0x06) { /* AliasOp */
			next = cdk2_coreboot_aml_skip_name(aml + 1U, end, NULL);
			if (next == NULL)
				return FALSE;
			next = cdk2_coreboot_aml_skip_name(next, end, NULL);
			if (next == NULL)
				return FALSE;
			aml = next;
			continue;
		}
		if (*aml == 0x15) { /* ExternalOp */
			next = cdk2_coreboot_aml_skip_name(aml + 1U, end, NULL);
			if (next == NULL || (UINTN)(end - next) < 2U)
				return FALSE;
			aml = next + 2U;
			continue;
		}
		if ((UINTN)(end - aml) >= 2U && aml[0] == 0x5b && aml[1] == 0x80) {
			/* OperationRegion(Name, Space, Offset, Length). */
			next = cdk2_coreboot_aml_skip_name(aml + 2U, end, NULL);
			if (next == NULL || next == end)
				return FALSE;
			next = cdk2_coreboot_aml_skip_termarg(next + 1U, end);
			if (next == NULL)
				return FALSE;
			next = cdk2_coreboot_aml_skip_termarg(next, end);
			if (next == NULL)
				return FALSE;
			aml = next;
			continue;
		}
		if (*aml == 0xa0 || *aml == 0xa1 || *aml == 0xa2) {
			/* If, Else, and While are executable packages, not namespace data. */
			if (!cdk2_coreboot_aml_package(aml + 1U, end, &body, &object_end))
				return FALSE;
			aml = object_end;
			continue;
		}
		if (*aml == 0x11 || *aml == 0x12 || *aml == 0x13 || *aml == 0x14) {
			/* Buffer, Package, VarPackage, and Method bodies are data/executable. */
			if (!cdk2_coreboot_aml_package(aml + 1U, end, &body, &object_end))
				return FALSE;
			aml = object_end;
			continue;
		}
		if (*aml == 0x10) { /* ScopeOp */
			BOOLEAN root_scope;
			if (!cdk2_coreboot_aml_package(aml + 1U, end, &body, &object_end))
				return FALSE;
			root_scope = (UINTN)(object_end - body) >= 2U &&
				body[0] == '\\' && body[1] == 0x00U;
			next = cdk2_coreboot_aml_skip_name(body, object_end, NULL);
			if (next == NULL)
				return FALSE;
			if (cdk2_coreboot_aml_find_s5(next, object_end, sleep_type,
			    root_scope ? depth : depth + 1U))
				return TRUE;
			aml = object_end;
			continue;
		}
		if ((UINTN)(end - aml) >= 2U && aml[0] == 0x5b &&
		    (aml[1] == 0x82 || aml[1] == 0x83 || aml[1] == 0x84 ||
		     aml[1] == 0x85)) { /* Device, Processor, PowerRes, ThermalZone */
			UINTN header_bytes = aml[1] == 0x83 ? 6U : aml[1] == 0x84 ? 3U : 0U;

			if (!cdk2_coreboot_aml_package(aml + 2U, end, &body, &object_end))
				return FALSE;
			next = cdk2_coreboot_aml_skip_name(body, object_end, NULL);
			if (next == NULL || (UINTN)(object_end - next) < header_bytes)
				return FALSE;
			if (cdk2_coreboot_aml_find_s5(next + header_bytes, object_end,
				    sleep_type, depth + 1U))
				return TRUE;
			aml = object_end;
			continue;
		}
		body = aml;
		if (cdk2_coreboot_acpi_read_integer(&body, end, &value)) {
			aml = body;
			continue;
		}
		/* Do not search arbitrary operand bytes. Unknown AML is rejected rather
		 * than being treated as a stream of possible namespace opcodes. */
		return FALSE;
	}
	return FALSE;
}

static BOOLEAN cdk2_coreboot_acpi_s5_sleep_type(
	const EFI_ACPI_DESCRIPTION_HEADER *dsdt, UINT8 *sleep_type)
{
	const UINT8 *aml;
	const UINT8 *end;

	if (dsdt == NULL || sleep_type == NULL ||
	    (dsdt->signature != SIGNATURE_32('D', 'S', 'D', 'T') &&
	     dsdt->signature != SIGNATURE_32('S', 'S', 'D', 'T')) ||
	    dsdt->length < sizeof(*dsdt) ||
	    dsdt->length > CDK2_COREBOOT_MAX_ACPI_TABLE_SIZE) {
		return FALSE;
	}
	aml = (const UINT8 *)(dsdt + 1);
	end = (const UINT8 *)dsdt + dsdt->length;
	parsed_s5_sleep_type_b = 0U;
	return cdk2_coreboot_aml_find_s5(aml, end, sleep_type, 0U);
}

static BOOLEAN cdk2_coreboot_acpi_root_s5(EFI_PHYSICAL_ADDRESS address,
	BOOLEAN extended, UINT8 *sleep_type)
{
	EFI_ACPI_DESCRIPTION_HEADER *root;
	EFI_ACPI_DESCRIPTION_HEADER *table;
	UINTN count, entry_size, index;
	UINT64 table_address;
	UINT32 signature;

	if (address == 0 || sleep_type == NULL)
		return FALSE;
	root = (void *)(UINTN)address;
	entry_size = extended ? sizeof(UINT64) : sizeof(UINT32);
	signature = extended ?
		EFI_ACPI_3_0_EXTENDED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE :
		EFI_ACPI_3_0_ROOT_SYSTEM_DESCRIPTION_TABLE_SIGNATURE;
	if (root->signature != signature || root->length < sizeof(*root) ||
	    root->length > CDK2_COREBOOT_MAX_ACPI_TABLE_SIZE ||
	    (root->length - sizeof(*root)) % entry_size != 0 ||
	    cdk2_coreboot_checksum8(root, root->length) != 0)
		return FALSE;
	count = (root->length - sizeof(*root)) / entry_size;
	for (index = 0; index < count; index++) {
		table_address = extended ? cdk2_coreboot_acpi_read64(
			(UINT8 *)(root + 1) + index * entry_size) :
			cdk2_coreboot_acpi_read32((UINT8 *)(root + 1) + index * entry_size);
		table = (void *)(UINTN)table_address;
		if (table != NULL && table->signature == SIGNATURE_32('S', 'S', 'D', 'T') &&
		    cdk2_coreboot_acpi_s5_sleep_type(table, sleep_type))
			return TRUE;
	}
	return FALSE;
}

static EFI_STATUS cdk2_coreboot_build_acpi_board_info(EFI_PHYSICAL_ADDRESS rsdp_address,
						      ACPI_BOARD_INFO *board_info,
						      EFI_PHYSICAL_ADDRESS *rsdp_base,
						      EFI_ACPI_DESCRIPTION_HEADER **tpm2_table,
						      const void **mcfg_table,
						      struct cdk2_acpi_rtc_info *rtc_info)
{
	EFI_ACPI_3_0_ROOT_SYSTEM_DESCRIPTION_POINTER *rsdp;
	EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE *fadt;
	EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER *mcfg;
	CDK2_ACPI_MCFG_ALLOCATION *mcfg_base;
	UINTN allocation_count;
	BOOLEAN tpm2_present;
	BOOLEAN tcpa_present;
	BOOLEAN extended_rsdp;
	EFI_ACPI_DESCRIPTION_HEADER *local_tpm2_table;
	struct cdk2_coreboot_acpi_root_result legacy_root;
	struct cdk2_coreboot_acpi_root_result extended_root;
	BOOLEAN have_extended_root;
	EFI_STATUS status;

	if (rsdp_address == 0 || board_info == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	rsdp = (EFI_ACPI_3_0_ROOT_SYSTEM_DESCRIPTION_POINTER *)(UINTN)rsdp_address;
	status = cdk2_coreboot_validate_rsdp(rsdp, &extended_rsdp);
	if (EFI_ERROR(status)) {
		return status;
	}

	fadt = NULL;
	board_info->pm_gpe_address_spaces_valid = 1U;
	mcfg = NULL;
	tpm2_present = FALSE;
	tcpa_present = FALSE;
	local_tpm2_table = NULL;

	have_extended_root = extended_rsdp && rsdp->xsdt_address != 0;
	if (have_extended_root) {
		status = cdk2_coreboot_acpi_parse_root(rsdp->xsdt_address, TRUE,
			&extended_root);
		if (EFI_ERROR(status))
			return status;
		if (rsdp->rsdt_address != 0) {
			status = cdk2_coreboot_acpi_parse_root(rsdp->rsdt_address, FALSE,
				&legacy_root);
			if (!EFI_ERROR(status) &&
			    !cdk2_coreboot_acpi_legacy_agrees(&legacy_root, &extended_root))
				return EFI_COMPROMISED_DATA;
		}
		fadt = extended_root.fadt;
		mcfg = extended_root.mcfg;
		tpm2_present = extended_root.tpm2_present;
		tcpa_present = extended_root.tcpa_present;
		local_tpm2_table = extended_root.tpm2_table;
	} else {
		if (rsdp->rsdt_address == 0)
			return EFI_NOT_FOUND;
		status = cdk2_coreboot_acpi_parse_root(rsdp->rsdt_address, FALSE,
			&legacy_root);
		if (EFI_ERROR(status))
			return status;
		fadt = legacy_root.fadt;
		mcfg = legacy_root.mcfg;
		tpm2_present = legacy_root.tpm2_present;
		tcpa_present = legacy_root.tcpa_present;
		local_tpm2_table = legacy_root.tpm2_table;
	}

	if (fadt == NULL) {
		return EFI_NOT_FOUND;
	}

	*board_info = (ACPI_BOARD_INFO){0};
	if (rtc_info != NULL) {
		*rtc_info = (struct cdk2_acpi_rtc_info) {
			.header = { CDK2_ACPI_RTC_INFO_REVISION, 0, sizeof(*rtc_info) },
		};
		/*
		 * Pre-v5 FADT retains the selected PC/AT compatibility path;
		 * its reserved boot-architecture bit 5 does not declare NO_CMOS.
		 */
		if (fadt->header.revision < 5U)
			rtc_info->flags |= CDK2_ACPI_RTC_LEGACY_PC_AT;
		else if (cdk2_coreboot_acpi_field_present(&fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, ia_pc_boot_arch),
		    sizeof(fadt->ia_pc_boot_arch))) {
			rtc_info->flags |= CDK2_ACPI_RTC_STATUS_KNOWN;
			if (fadt->ia_pc_boot_arch & BIT5)
				rtc_info->flags |= CDK2_ACPI_RTC_NO_CMOS;
		}
		if (cdk2_coreboot_acpi_field_present(&fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, century),
		    sizeof(fadt->century))) {
			rtc_info->flags |= CDK2_ACPI_RTC_CENTURY_KNOWN;
			rtc_info->century_index = fadt->century;
		}
	}
	board_info->pm_ctrl_reg_base = fadt->pm1a_cnt_blk;
	board_info->pm_ctrl_reg_base_b = fadt->pm1b_cnt_blk;
	board_info->pm_ctrl_reg_address_space = 1U; /* System I/O address space. */
	board_info->pm_ctrl_reg_b_address_space = 1U;
	if (board_info->pm_ctrl_reg_base == 0 &&
	    cdk2_coreboot_acpi_field_present(
		    &fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, x_pm1a_cnt_blk),
		    sizeof(fadt->x_pm1a_cnt_blk)) && fadt->x_pm1a_cnt_blk.address != 0) {
		board_info->pm_ctrl_reg_base = fadt->x_pm1a_cnt_blk.address;
		board_info->pm_ctrl_reg_address_space = fadt->x_pm1a_cnt_blk.address_space_id;
	}
	if (board_info->pm_ctrl_reg_base_b == 0 &&
	    cdk2_coreboot_acpi_field_present(&fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, x_pm1b_cnt_blk),
		    sizeof(fadt->x_pm1b_cnt_blk)) && fadt->x_pm1b_cnt_blk.address != 0) {
		board_info->pm_ctrl_reg_base_b = fadt->x_pm1b_cnt_blk.address;
		board_info->pm_ctrl_reg_b_address_space = fadt->x_pm1b_cnt_blk.address_space_id;
	}
	board_info->pm_timer_reg_base = fadt->pm_tmr_blk;
	board_info->pm_evt_base = fadt->pm1a_evt_blk;
	board_info->pm_evt_base_b = fadt->pm1b_evt_blk;
	board_info->pm_evt_address_space = 1U;
	board_info->pm_evt_b_address_space = 1U;
	board_info->pm_evt_length = fadt->pm1_evt_len;
	if (board_info->pm_evt_base == 0U &&
	    cdk2_coreboot_acpi_field_present(&fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, x_pm1a_evt_blk),
		    sizeof(fadt->x_pm1a_evt_blk)) && fadt->x_pm1a_evt_blk.address != 0U) {
		board_info->pm_evt_base = fadt->x_pm1a_evt_blk.address;
		board_info->pm_evt_address_space = fadt->x_pm1a_evt_blk.address_space_id;
	}
	if (board_info->pm_evt_base_b == 0U &&
	    cdk2_coreboot_acpi_field_present(&fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, x_pm1b_evt_blk),
		    sizeof(fadt->x_pm1b_evt_blk)) && fadt->x_pm1b_evt_blk.address != 0U) {
		board_info->pm_evt_base_b = fadt->x_pm1b_evt_blk.address;
		board_info->pm_evt_b_address_space = fadt->x_pm1b_evt_blk.address_space_id;
	}
	if (cdk2_coreboot_acpi_field_present(&fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, flags),
		    sizeof(fadt->flags)) && (fadt->flags & BIT20) != 0U &&
	    cdk2_coreboot_acpi_field_present(&fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, sleep_control_reg),
		    sizeof(fadt->sleep_control_reg)) && fadt->sleep_control_reg.address != 0U) {
		board_info->hardware_reduced_acpi = 1U;
		board_info->sleep_control_reg = fadt->sleep_control_reg.address;
		board_info->sleep_control_address_space =
			fadt->sleep_control_reg.address_space_id;
		if (cdk2_coreboot_acpi_field_present(&fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, sleep_status_reg),
		    sizeof(fadt->sleep_status_reg)) && fadt->sleep_status_reg.address != 0U) {
			board_info->sleep_status_reg = fadt->sleep_status_reg.address;
			board_info->sleep_status_address_space =
				fadt->sleep_status_reg.address_space_id;
		}
	}
	if (fadt->gpe0_blk != 0U) {
		board_info->pm_gpe_en_base = fadt->gpe0_blk + fadt->gpe0_blk_len / 2;
		board_info->pm_gpe_en_length = fadt->gpe0_blk_len / 2;
		board_info->pm_gpe_en_address_space = 1U;
	} else if (cdk2_coreboot_acpi_field_present(&fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, x_gpe0_blk),
		    sizeof(fadt->x_gpe0_blk)) && fadt->x_gpe0_blk.address != 0U) {
		board_info->pm_gpe_en_base = fadt->x_gpe0_blk.address +
			fadt->gpe0_blk_len / 2U;
		board_info->pm_gpe_en_length = fadt->gpe0_blk_len / 2U;
		board_info->pm_gpe_en_address_space =
			fadt->x_gpe0_blk.address_space_id;
	}
	if (cdk2_coreboot_acpi_field_present(&fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, gpe1_blk_len),
		    sizeof(fadt->gpe1_blk_len)) && fadt->gpe1_blk != 0U) {
		board_info->pm_gpe1_en_base = fadt->gpe1_blk + fadt->gpe1_blk_len / 2;
		board_info->pm_gpe1_en_length = fadt->gpe1_blk_len / 2;
		board_info->pm_gpe1_en_address_space = 1U;
	} else if (cdk2_coreboot_acpi_field_present(&fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, x_gpe1_blk),
		    sizeof(fadt->x_gpe1_blk)) && fadt->x_gpe1_blk.address != 0U) {
		board_info->pm_gpe1_en_base = fadt->x_gpe1_blk.address +
			fadt->gpe1_blk_len / 2U;
		board_info->pm_gpe1_en_length = fadt->gpe1_blk_len / 2U;
		board_info->pm_gpe1_en_address_space =
			fadt->x_gpe1_blk.address_space_id;
	}
	if (cdk2_coreboot_acpi_field_present(
		    &fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, flags),
		    sizeof(fadt->flags)) && (fadt->flags & BIT10) != 0 &&
	    cdk2_coreboot_acpi_field_present(
		    &fadt->header,
		    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, reset_value),
		    sizeof(fadt->reset_value))) {
		board_info->reset_reg_address = fadt->reset_reg.address;
		board_info->reset_reg_address_space = fadt->reset_reg.address_space_id;
		board_info->reset_value = fadt->reset_value;
	}
	{
		EFI_PHYSICAL_ADDRESS dsdt_address = fadt->dsdt;

		if (cdk2_coreboot_acpi_field_present(
			    &fadt->header,
			    OFFSET_OF(EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE, x_dsdt),
			    sizeof(fadt->x_dsdt)) && fadt->x_dsdt != 0) {
			dsdt_address = fadt->x_dsdt;
		}
		board_info->s5_sleep_type_valid = cdk2_coreboot_acpi_s5_sleep_type(
			(const EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)dsdt_address,
			&board_info->s5_sleep_type);
		if (!board_info->s5_sleep_type_valid && extended_rsdp &&
		    rsdp->xsdt_address != 0)
			board_info->s5_sleep_type_valid = cdk2_coreboot_acpi_root_s5(
				rsdp->xsdt_address, TRUE, &board_info->s5_sleep_type);
		if (!board_info->s5_sleep_type_valid && rsdp->rsdt_address != 0)
			board_info->s5_sleep_type_valid = cdk2_coreboot_acpi_root_s5(
				rsdp->rsdt_address, FALSE, &board_info->s5_sleep_type);
		board_info->s5_sleep_type_b = parsed_s5_sleep_type_b;
		board_info->s5_sleep_type_b_valid = board_info->s5_sleep_type_valid;
	}

	board_info->tpm20_present = tpm2_present;
	board_info->tpm12_present = tcpa_present;
	if (mcfg != NULL) {
		allocation_count = mcfg->header.length - sizeof(*mcfg);
		allocation_count /= sizeof(CDK2_ACPI_MCFG_ALLOCATION);
		if (allocation_count == 0) {
			return EFI_COMPROMISED_DATA;
		}

		mcfg_base = (CDK2_ACPI_MCFG_ALLOCATION *)(mcfg + 1);
		for (UINTN index = 0; index < allocation_count; index++) {
			if (mcfg_base[index].end_bus_number <
			    mcfg_base[index].start_bus_number)
				return EFI_COMPROMISED_DATA;
			if (mcfg_base[index].pci_segment_group_number != 0U ||
			    board_info->pcie_base_size != 0U)
				continue;
			board_info->pcie_base_address = mcfg_base[index].base_address;
			board_info->pcie_base_size =
				(UINT64)(mcfg_base[index].end_bus_number + 1U -
					 mcfg_base[index].start_bus_number) << 20;
			board_info->pcie_start_bus = mcfg_base[index].start_bus_number;
		}
	}
	if (mcfg_table != NULL)
		*mcfg_table = mcfg;

	if (rsdp_base != NULL) {
		*rsdp_base = rsdp_address;
	}

	if (tpm2_table != NULL) {
		*tpm2_table = local_tpm2_table;
	}

	return EFI_SUCCESS;
}

static UINTN cdk2_coreboot_string_record_length(const struct cb_string *string)
{
	UINTN length;

	if (string == NULL || string->size < sizeof(*string)) {
		return 0;
	}

	length = string->size - sizeof(*string);
	if (length != 0 && string->string[length - 1] == '\0') {
		length--;
	}

	return length;
}

static EFI_STATUS cdk2_coreboot_append_acpi_table_hob(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
						      EFI_PHYSICAL_ADDRESS rsdp_base)
{
	CDK2_ACPI_TABLE_HOB acpi_table;

	if (rsdp_base == 0) {
		return EFI_INVALID_PARAMETER;
	}

	acpi_table = (CDK2_ACPI_TABLE_HOB){0};
	acpi_table.header.revision = CDK2_ACPI_TABLE_HOB_REVISION;
	acpi_table.header.length = sizeof(acpi_table);
	acpi_table.rsdp = rsdp_base;

	return cdk2_coreboot_append_guid_hob(handoff, &m_cdk2_acpi_table_guid, &acpi_table,
					     sizeof(acpi_table));
}

static EFI_STATUS cdk2_coreboot_append_smram_hob(
	EFI_HOB_HANDOFF_INFO_TABLE *handoff,
	const struct cdk2_coreboot_handoff *coreboot)
{
	const struct cb_smram *smram;
	const void *record;
	EFI_SMRAM_HOB_DESCRIPTOR_BLOCK smram_info;
	EFI_STATUS status;

	status = cdk2_coreboot_find_record(coreboot, CB_TAG_SMRAM,
		CDK2_COREBOOT_SMRAM_MIN_SIZE, &record);
	if (EFI_ERROR(status))
		return status;
	smram = (const struct cb_smram *)record;
	if (smram->physical_start == 0 || smram->physical_size == 0 ||
	    smram->physical_start > MAX_UINT64 - smram->physical_size)
		return EFI_COMPROMISED_DATA;

	smram_info = (EFI_SMRAM_HOB_DESCRIPTOR_BLOCK){0};
	smram_info.number_of_smm_reserved_regions = 1;
	smram_info.descriptor[0].physical_start = smram->physical_start;
	smram_info.descriptor[0].cpu_start = smram->physical_start;
	smram_info.descriptor[0].physical_size = smram->physical_size;
	smram_info.descriptor[0].region_state = EFI_SMRAM_CLOSED | EFI_CACHEABLE;
	return cdk2_coreboot_append_guid_hob(handoff, &m_cdk2_smram_memory_guid,
		&smram_info, sizeof(smram_info));
}

static EFI_STATUS cdk2_coreboot_append_smm_register_info_hob(
	EFI_HOB_HANDOFF_INFO_TABLE *handoff,
	const struct cdk2_coreboot_handoff *coreboot)
{
	const struct cb_smm_register_info *info;
	const void *record;
	UINTN payload_size;
	EFI_STATUS status;

	status = cdk2_coreboot_find_record(coreboot, CB_TAG_SMM_REGISTER_INFO,
		CDK2_COREBOOT_SMM_REGISTER_INFO_MIN_SIZE, &record);
	if (EFI_ERROR(status))
		return status;
	info = (const struct cb_smm_register_info *)record;
	if (info->revision != CDK2_SMM_REGISTER_INFO_REVISION || info->reserved != 0U ||
	    info->count == 0U || info->count > CDK2_SMM_REGISTER_MAX_COUNT)
		return EFI_COMPROMISED_DATA;
	payload_size = sizeof(*info) - sizeof(struct cb_record) +
		(UINTN)info->count * sizeof(info->registers[0]);
	if (payload_size != info->size - sizeof(struct cb_record))
		return EFI_COMPROMISED_DATA;
	return cdk2_coreboot_append_guid_hob(handoff, &m_cdk2_smm_register_info_guid,
		&info->revision, payload_size);
}

static EFI_STATUS cdk2_coreboot_append_s3_communication_hob(
	EFI_HOB_HANDOFF_INFO_TABLE *handoff,
	const struct cdk2_coreboot_handoff *coreboot)
{
	const struct cb_s3_communication *record;
	const void *raw;
	struct {
		EFI_SMRAM_DESCRIPTOR buffer;
		BOOLEAN payload_s3_enabled;
	} __packed communication;
	EFI_STATUS status;
	UINTN index;

	status = cdk2_coreboot_find_record(coreboot, CB_TAG_S3_COMMUNICATION,
		CDK2_COREBOOT_S3_COMMUNICATION_MIN_SIZE, &raw);
	if (EFI_ERROR(status))
		return status;
	record = raw;
	for (index = 0U; index < sizeof(record->reserved); index++)
		if (record->reserved[index] != 0U)
			return EFI_COMPROMISED_DATA;
	if (record->payload_s3_enabled > 1U || record->physical_start == 0U ||
	    record->physical_size == 0U ||
	    record->physical_start > MAX_UINT64 - record->physical_size)
		return EFI_COMPROMISED_DATA;
	communication.buffer = (EFI_SMRAM_DESCRIPTOR){0};
	communication.buffer.physical_start = record->physical_start;
	communication.buffer.cpu_start = record->physical_start;
	communication.buffer.physical_size = record->physical_size;
	communication.buffer.region_state = EFI_SMRAM_CLOSED | EFI_CACHEABLE;
	communication.payload_s3_enabled = record->payload_s3_enabled;
	return cdk2_coreboot_append_guid_hob(handoff,
		&m_cdk2_s3_communication_guid, &communication,
		sizeof(communication));
}

#if CONFIG_CDK2_NATIVE_LOCAL_APIC_TIMER
static EFI_STATUS cdk2_coreboot_append_local_apic_timer_info_hob(
	EFI_HOB_HANDOFF_INFO_TABLE *handoff,
	const struct cdk2_coreboot_handoff *coreboot)
{
	const struct cb_local_apic_timer_info *record;
	struct cdk2_local_apic_timer_info info;
	const void *raw;
	EFI_STATUS status;

	status = cdk2_coreboot_find_unique_record(coreboot, CB_TAG_LOCAL_APIC_TIMER_INFO,
		CDK2_COREBOOT_LOCAL_APIC_TIMER_INFO_MIN_SIZE, &raw);
	if (EFI_ERROR(status))
		return status;
	record = raw;
	if (record->revision != CDK2_LOCAL_APIC_TIMER_INFO_REVISION ||
	    record->reserved != 0U || record->frequency_hz == 0U ||
	    record->frequency_hz > MAX_UINT32)
		return EFI_COMPROMISED_DATA;
	info = (struct cdk2_local_apic_timer_info) {
		.revision = record->revision,
		.frequency_hz = record->frequency_hz,
	};
	return cdk2_coreboot_append_guid_hob(handoff,
		&m_cdk2_local_apic_timer_info_guid, &info, sizeof(info));
}
#endif

static UINT16 cdk2_coreboot_tpm_digest_size(TPMI_ALG_HASH hash_alg)
{
	switch (hash_alg) {
	case TPM_ALG_SHA1:
		return SHA1_DIGEST_SIZE;
	case TPM_ALG_SHA256:
		return SHA256_DIGEST_SIZE;
	case TPM_ALG_SHA384:
		return SHA384_DIGEST_SIZE;
	case TPM_ALG_SHA512:
		return SHA512_DIGEST_SIZE;
	case TPM_ALG_SM3_256:
		return SM3_256_DIGEST_SIZE;
	default:
		return 0;
	}
}

static BOOLEAN cdk2_coreboot_get_tpm2_acpi_event_log(const EFI_ACPI_DESCRIPTION_HEADER *table,
						     UINT32 *laml, EFI_PHYSICAL_ADDRESS *lasa)
{
	UINTN log_area_offset;
	UINTN parameters_size;

	if (table == NULL || laml == NULL || lasa == NULL ||
	    table->signature != EFI_ACPI_5_0_TRUSTED_COMPUTING_PLATFORM_2_TABLE_SIGNATURE ||
	    table->revision < EFI_TPM2_ACPI_TABLE_REVISION_4 ||
	    table->length < sizeof(EFI_TPM2_ACPI_TABLE)) {
		return FALSE;
	}

	parameters_size =
		EFI_TPM2_ACPI_TABLE_START_METHOD_SPECIFIC_PARAMETERS_MAX_SIZE_REVISION_4;
	if (table->revision >= EFI_TPM2_ACPI_TABLE_REVISION_5) {
		parameters_size =
			EFI_TPM2_ACPI_TABLE_START_METHOD_SPECIFIC_PARAMETERS_MAX_SIZE_REVISION_5;
	}

	log_area_offset = sizeof(EFI_TPM2_ACPI_TABLE) + parameters_size;
	if (table->length < log_area_offset + sizeof(*laml) + sizeof(*lasa)) {
		return FALSE;
	}

	*laml = cdk2_coreboot_read32((const UINT8 *)table + log_area_offset);
	*lasa = cdk2_coreboot_read64((const UINT8 *)table + log_area_offset + sizeof(*laml));
	return *laml != 0 && *lasa != 0 && *laml <= CDK2_COREBOOT_MAX_TPM2_LOG_SIZE;
}

static BOOLEAN cdk2_coreboot_get_tcg_pcr_event2_size(const UINT8 *event, UINTN remaining,
						     UINTN *event_size, UINTN *event_data_offset,
						     UINT32 *event_data_size)
{
	UINT32 count;
	UINT32 pcr_index;
	UINT16 digest_size;
	TPMI_ALG_HASH hash_alg;
	UINTN index;
	UINTN offset;

	if (event == NULL || event_size == NULL || event_data_offset == NULL ||
	    event_data_size == NULL) {
		return FALSE;
	}

	offset = sizeof(TCG_PCRINDEX) + sizeof(TCG_EVENTTYPE);
	if (remaining < offset + sizeof(count) + sizeof(*event_data_size)) {
		return FALSE;
	}

	pcr_index = cdk2_coreboot_read32(event);
	if (pcr_index > CDK2_COREBOOT_MAX_TPM_PCR_INDEX) {
		return FALSE;
	}

	count = cdk2_coreboot_read32(event + offset);
	offset += sizeof(count);
	if (count == 0 || count > HASH_COUNT) {
		return FALSE;
	}

	for (index = 0; index < count; index++) {
		if (remaining < offset + sizeof(hash_alg)) {
			return FALSE;
		}

		hash_alg = cdk2_coreboot_read16(event + offset);
		offset += sizeof(hash_alg);

		digest_size = cdk2_coreboot_tpm_digest_size(hash_alg);
		if (digest_size == 0 || remaining < offset + digest_size) {
			return FALSE;
		}

		offset += digest_size;
	}

	if (remaining < offset + sizeof(*event_data_size)) {
		return FALSE;
	}

	*event_data_size = cdk2_coreboot_read32(event + offset);
	offset += sizeof(*event_data_size);
	if (remaining < offset + *event_data_size) {
		return FALSE;
	}

	*event_data_offset = offset;
	*event_size = offset + *event_data_size;
	return TRUE;
}

static BOOLEAN cdk2_coreboot_is_spec_id_event(const UINT8 *event_data, UINT32 event_size)
{
	if (event_size < sizeof(CDK2_COREBOOT_SPEC_ID_EVENT_NAME) - 1) {
		return FALSE;
	}

	return cdk2_coreboot_bytes_equal(event_data, CDK2_COREBOOT_SPEC_ID_EVENT_NAME,
					 sizeof(CDK2_COREBOOT_SPEC_ID_EVENT_NAME) - 1);
}

static UINTN cdk2_coreboot_get_first_tcg_pcr_event2_offset(const UINT8 *event_log,
							   UINTN event_log_size)
{
	UINT32 event_data_size;
	UINTN event_size_offset;
	UINTN first_event_offset;

	if (event_log == NULL || event_log_size < sizeof(TCG_PCR_EVENT_HDR)) {
		return 0;
	}

	if (cdk2_coreboot_read32(event_log + sizeof(TCG_PCRINDEX)) != EV_NO_ACTION) {
		return 0;
	}

	event_size_offset = sizeof(TCG_PCRINDEX) + sizeof(TCG_EVENTTYPE) + sizeof(TCG_DIGEST);
	event_data_size = cdk2_coreboot_read32(event_log + event_size_offset);
	first_event_offset = sizeof(TCG_PCR_EVENT_HDR) + event_data_size;
	if (event_log_size < first_event_offset ||
	    !cdk2_coreboot_is_spec_id_event(event_log + sizeof(TCG_PCR_EVENT_HDR),
					    event_data_size)) {
		return 0;
	}

	return first_event_offset;
}

static EFI_STATUS cdk2_coreboot_append_tpm_event_hobs(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
						      EFI_ACPI_DESCRIPTION_HEADER *tpm2_table)
{
	UINT8 *event_log;
	UINTN event_data_offset;
	UINT32 event_data_size;
	UINTN event_size;
	UINT32 laml;
	EFI_PHYSICAL_ADDRESS lasa;
	UINT32 event_type;
	UINTN offset;
	EFI_STATUS status;

	if (tpm2_table == NULL) {
		return EFI_SUCCESS;
	}

	if (!cdk2_coreboot_get_tpm2_acpi_event_log(tpm2_table, &laml, &lasa)) {
		return EFI_SUCCESS;
	}

	event_log = (UINT8 *)(UINTN)lasa;
	offset = cdk2_coreboot_get_first_tcg_pcr_event2_offset(event_log, laml);
	while (offset < laml) {
		if (!cdk2_coreboot_get_tcg_pcr_event2_size(event_log + offset, laml - offset,
							   &event_size, &event_data_offset,
							   &event_data_size)) {
			break;
		}

		event_type = cdk2_coreboot_read32(event_log + offset + sizeof(TCG_PCRINDEX));
		if (event_type != EV_NO_ACTION ||
		    !cdk2_coreboot_is_spec_id_event(event_log + offset + event_data_offset,
						    event_data_size)) {
			status = cdk2_coreboot_append_guid_hob(handoff,
							       &m_cdk2_tcg_event2_entry_hob_guid,
							       event_log + offset, event_size);
			if (EFI_ERROR(status)) {
				return status;
			}
		}

		offset += event_size;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS cdk2_coreboot_find_acpi_rsdp(const struct cdk2_coreboot_handoff *coreboot,
					       EFI_PHYSICAL_ADDRESS *rsdp_base)
{
	const void *record;
	const struct cb_acpi_rsdp *rsdp_record;
	EFI_STATUS status;

	if (coreboot == NULL || rsdp_base == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	status = cdk2_coreboot_find_unique_record(coreboot, CB_TAG_ACPI_RSDP,
					   CDK2_COREBOOT_ACPI_RSDP_MIN_SIZE, &record);
	if (!EFI_ERROR(status)) {
		rsdp_record = (const struct cb_acpi_rsdp *)record;
		*rsdp_base = (EFI_PHYSICAL_ADDRESS)rsdp_record->rsdp_pointer.lo |
			    ((EFI_PHYSICAL_ADDRESS)rsdp_record->rsdp_pointer.hi << 32);
		return (*rsdp_base != 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
	}

	if (status != EFI_NOT_FOUND) {
		return status;
	}

	return cdk2_coreboot_find_cbmem_entry(
		coreboot, CBMEM_ID_ACPI, CDK2_COREBOOT_ACPI_RSDP_V1_SIZE,
		rsdp_base, NULL);
}

static EFI_STATUS cdk2_coreboot_update_acpi_handoff(struct cdk2_coreboot_handoff *coreboot)
{
	ACPI_BOARD_INFO board_info;
	EFI_PHYSICAL_ADDRESS rsdp_base;
	EFI_STATUS status;

	if (coreboot == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	status = cdk2_coreboot_find_acpi_rsdp(coreboot, &rsdp_base);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = cdk2_coreboot_build_acpi_board_info(rsdp_base, &board_info, NULL, NULL,
		&coreboot->mcfg, NULL);
	if (EFI_ERROR(status)) {
		return status;
	}

	coreboot->pcie_base_address = board_info.pcie_base_address;
	coreboot->pcie_base_size = board_info.pcie_base_size;
	coreboot->pcie_start_bus = board_info.pcie_start_bus;
	return EFI_SUCCESS;
}

static EFI_STATUS cdk2_coreboot_append_acpi_hobs(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
						 const struct cdk2_coreboot_handoff *coreboot)
{
	struct cdk2_acpi_board_info_hob board_hob;
	EFI_PHYSICAL_ADDRESS rsdp_base;
	EFI_ACPI_DESCRIPTION_HEADER *tpm2_table;
	EFI_STATUS status;

	status = cdk2_coreboot_find_acpi_rsdp(coreboot, &rsdp_base);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = cdk2_coreboot_build_acpi_board_info(rsdp_base, &board_hob.board_info, &rsdp_base,
		&tpm2_table, NULL, &board_hob.rtc);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = cdk2_coreboot_append_acpi_table_hob(handoff, rsdp_base);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = cdk2_coreboot_append_guid_hob(handoff, &m_cdk2_acpi_board_info_hob_guid, &board_hob,
					       sizeof(board_hob));
	if (EFI_ERROR(status)) {
		return status;
	}

	return cdk2_coreboot_append_tpm_event_hobs(handoff, tpm2_table);
}

static EFI_STATUS
cdk2_coreboot_append_coreboot_table_hob(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
					const struct cdk2_coreboot_handoff *coreboot)
{
	COREBOOT_TABLE_HOB table_hob;

	if (coreboot == NULL || coreboot->header == NULL || coreboot->table_size > MAX_UINT32) {
		return EFI_INVALID_PARAMETER;
	}

	table_hob = (COREBOOT_TABLE_HOB){0};
	table_hob.address = (UINT64)(UINTN)coreboot->header;
	table_hob.size = (UINT32)coreboot->table_size;

	return cdk2_coreboot_append_guid_hob(handoff, &m_cdk2_coreboot_table_guid, &table_hob,
					     sizeof(table_hob));
}

static EFI_STATUS
cdk2_coreboot_find_legacy_cbmem_table(const struct cbmem_root *root, UINT32 id,
				      UINT32 minimum_size, EFI_PHYSICAL_ADDRESS *base,
				      UINT32 *size)
{
	const struct cbmem_entry *entries;
	const struct imd_entry *imd_entries;
	UINTN header_size;
	UINTN entry_size;
	UINTN max_entries;
	UINTN index;
	BOOLEAN is_imd_entry;
	INTN offset;
	UINTN root_base;
	UINT32 cbmem_size;

	if (root == NULL || base == NULL || root->num_entries == 0 ||
	    root->num_entries > root->max_entries) {
		return EFI_NOT_FOUND;
	}

	entries = root->entries;
	header_size = sizeof(*root);
	entry_size = sizeof(*entries);
	imd_entries = NULL;
	is_imd_entry = FALSE;
	if (entries[0].magic != CBMEM_ENTRY_MAGIC) {
		imd_entries = ((const struct imd_root *)root)->entries;
		header_size = sizeof(struct imd_root);
		entry_size = sizeof(*imd_entries);
		if (imd_entries[0].magic != IMD_ENTRY_MAGIC) {
			return EFI_NOT_FOUND;
		}

		is_imd_entry = TRUE;
	}

	if (header_size > DYN_CBMEM_ALIGN_SIZE) {
		return EFI_COMPROMISED_DATA;
	}

	max_entries = (DYN_CBMEM_ALIGN_SIZE - header_size) / entry_size;
	if (root->num_entries > max_entries) {
		return EFI_COMPROMISED_DATA;
	}

	root_base = (UINTN)root;
	for (index = 0; index < root->num_entries; index++) {
		if (is_imd_entry) {
			if (imd_entries[index].id != id) {
				continue;
			}

			cbmem_size = imd_entries[index].size;
			if (cbmem_size < minimum_size) {
				return EFI_COMPROMISED_DATA;
			}

			offset = (INTN)(INT32)imd_entries[index].start_offset;
			if ((offset < 0 && root_base < (UINTN)-offset) ||
			    (offset > 0 && root_base > MAX_UINTN - (UINTN)offset)) {
				return EFI_COMPROMISED_DATA;
			}

			*base = (EFI_PHYSICAL_ADDRESS)(root_base + offset);
		} else {
			if (entries[index].id != id) {
				continue;
			}

			cbmem_size = entries[index].size;
			if (cbmem_size < minimum_size) {
				return EFI_COMPROMISED_DATA;
			}

			*base = entries[index].start;
		}

		if (*base == 0 || *base > MAX_UINT64 - cbmem_size) {
			return EFI_COMPROMISED_DATA;
		}

		if (size != NULL) {
			*size = cbmem_size;
		}

		return EFI_SUCCESS;
	}

	return EFI_NOT_FOUND;
}

static EFI_STATUS cdk2_coreboot_find_legacy_cbmem_entry(
	const struct cdk2_coreboot_handoff *coreboot, UINT32 id, UINT32 minimum_size,
	EFI_PHYSICAL_ADDRESS *base, UINT32 *size)
{
	const struct cdk2_coreboot_memory_range *range;
	const struct cbmem_root *root;
	UINT64 end;
	UINTN index;
	EFI_STATUS status;

	if (coreboot == NULL || base == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	for (index = 0; index < coreboot->memory_range_count; index++) {
		range = &coreboot->memory_ranges[index];
		if (range->type != CB_MEM_TABLE || range->base <= 0x1000 ||
		    range->size < DYN_CBMEM_ALIGN_SIZE ||
		    range->base > MAX_UINT64 - range->size) {
			continue;
		}

		end = range->base + range->size;
		root = (const struct cbmem_root *)(UINTN)(end - DYN_CBMEM_ALIGN_SIZE);
		status = cdk2_coreboot_find_legacy_cbmem_table(root, id, minimum_size, base, size);
		if (status != EFI_NOT_FOUND) {
			return status;
		}
	}

	return EFI_NOT_FOUND;
}

struct cdk2_deadline_cbmem_match {
	EFI_PHYSICAL_ADDRESS base;
	UINT32 size;
	UINTN count;
};

static EFI_STATUS cdk2_coreboot_record_legacy_deadline(
	struct cdk2_deadline_cbmem_match *match,
	EFI_PHYSICAL_ADDRESS entry_base, UINT32 entry_size)
{
	if (entry_size < sizeof(struct cdk2_diag_timestamp_table)) {
		return EFI_COMPROMISED_DATA;
	}

	match->count++;
	if (match->count > 1U) {
		return EFI_COMPROMISED_DATA;
	}

	match->base = entry_base;
	match->size = entry_size;
	return EFI_SUCCESS;
}

static EFI_STATUS cdk2_coreboot_scan_legacy_deadline_root(
	const struct cbmem_root *root, EFI_PHYSICAL_ADDRESS range_base,
	EFI_PHYSICAL_ADDRESS range_end, struct cdk2_deadline_cbmem_match *match)
{
	const struct cbmem_entry *cbmem_entries;
	const struct imd_entry *entries;
	EFI_PHYSICAL_ADDRESS entry_base;
	EFI_PHYSICAL_ADDRESS entry_end;
	EFI_STATUS status;
	UINT32 entry_id;
	UINT32 entry_size;
	UINTN root_base;
	UINTN capacity;
	UINTN header_size;
	UINTN index;
	INTN offset;
	BOOLEAN is_imd;

	if (root->num_entries > root->max_entries) {
		return EFI_COMPROMISED_DATA;
	}

	if (root->num_entries == 0U) {
		capacity = (DYN_CBMEM_ALIGN_SIZE - sizeof(*root)) /
			sizeof(root->entries[0]);
		if (root->max_entries > capacity) {
			return EFI_COMPROMISED_DATA;
		}
		return EFI_SUCCESS;
	}

	cbmem_entries = root->entries;
	entries = ((const struct imd_root *)root)->entries;
	if (cbmem_entries[0].magic == CBMEM_ENTRY_MAGIC) {
		header_size = sizeof(*root);
		is_imd = FALSE;
	} else if (entries[0].magic == IMD_ENTRY_MAGIC) {
		header_size = sizeof(struct imd_root);
		is_imd = TRUE;
	} else {
		return EFI_COMPROMISED_DATA;
	}

	capacity = (DYN_CBMEM_ALIGN_SIZE - header_size) /
		sizeof(root->entries[0]);
	if (root->max_entries > capacity) {
		return EFI_COMPROMISED_DATA;
	}

	root_base = (UINTN)root;
	for (index = 0U; index < root->num_entries; index++) {
		if (is_imd) {
			if (entries[index].magic != IMD_ENTRY_MAGIC) {
				return EFI_COMPROMISED_DATA;
			}
			entry_id = entries[index].id;
			entry_size = entries[index].size;
		} else {
			if (cbmem_entries[index].magic != CBMEM_ENTRY_MAGIC) {
				return EFI_COMPROMISED_DATA;
			}
			entry_id = cbmem_entries[index].id;
			entry_size = cbmem_entries[index].size;
		}
		if (is_imd) {
			offset = (INTN)(INT32)entries[index].start_offset;
			if ((offset < 0 && root_base < (UINTN)-offset) ||
			    (offset > 0 &&
			     root_base > MAX_UINTN - (UINTN)offset)) {
				return EFI_COMPROMISED_DATA;
			}
			entry_base = (EFI_PHYSICAL_ADDRESS)(root_base + offset);
		} else {
			entry_base = cbmem_entries[index].start;
		}

		if (entry_size == 0U || entry_base == 0U ||
		    entry_base > MAX_UINT64 - entry_size) {
			return EFI_COMPROMISED_DATA;
		}
		entry_end = entry_base + entry_size;
		if (entry_base < range_base || entry_end > range_end) {
			return EFI_COMPROMISED_DATA;
		}
		if (entry_id != CBMEM_ID_TIMESTAMP) {
			continue;
		}

		status = cdk2_coreboot_record_legacy_deadline(match, entry_base,
			entry_size);
		if (EFI_ERROR(status)) {
			return status;
		}
	}

	return EFI_SUCCESS;
}

/* Unlike generic CBMEM lookup, the deadline fallback examines every eligible
 * root and rejects competing TIME allocations. */
static EFI_STATUS cdk2_coreboot_find_unique_legacy_deadline(
	const struct cdk2_coreboot_handoff *coreboot, EFI_PHYSICAL_ADDRESS *base,
	UINT32 *size)
{
	struct cdk2_deadline_cbmem_match match;
	const struct cdk2_coreboot_memory_range *range;
	const struct cbmem_root *root;
	EFI_PHYSICAL_ADDRESS range_end;
	EFI_STATUS status;
	UINTN index;

	if (coreboot == NULL || base == NULL || size == NULL ||
	    coreboot->memory_range_count > CDK2_COREBOOT_MAX_MEMORY_RANGES) {
		return EFI_INVALID_PARAMETER;
	}

	match = (struct cdk2_deadline_cbmem_match){0};
	for (index = 0U; index < coreboot->memory_range_count; index++) {
		range = &coreboot->memory_ranges[index];
		if (range->type != CB_MEM_TABLE || range->base <= 0x1000U ||
		    range->size < DYN_CBMEM_ALIGN_SIZE ||
		    range->base > MAX_UINT64 - range->size) {
			continue;
		}

		range_end = range->base + range->size;
		root = (const void *)(UINTN)(range_end - DYN_CBMEM_ALIGN_SIZE);
		status = cdk2_coreboot_scan_legacy_deadline_root(root,
			range->base, range_end, &match);
		if (EFI_ERROR(status)) {
			return status;
		}
	}

	if (match.count == 0U) {
		return EFI_NOT_FOUND;
	}

	*base = match.base;
	*size = match.size;
	return EFI_SUCCESS;
}

static EFI_STATUS cdk2_coreboot_find_unique_modern_cbmem_entry(
	const struct cdk2_coreboot_handoff *coreboot, UINT32 id,
	UINT32 minimum_size, EFI_PHYSICAL_ADDRESS *base, UINT32 *size)
{
	const struct cb_record *record;
	const struct cb_cbmem_entry *entry;
	const UINT8 *cursor;
	EFI_PHYSICAL_ADDRESS found_base;
	UINT32 found_size;
	UINTN remaining;
	UINTN index;
	BOOLEAN found;

	if (coreboot == NULL || coreboot->header == NULL || base == NULL) {
		return EFI_INVALID_PARAMETER;
	}
	if (coreboot->header->header_bytes > coreboot->table_size) {
		return EFI_COMPROMISED_DATA;
	}

	cursor = (const UINT8 *)coreboot->header + coreboot->header->header_bytes;
	remaining = coreboot->table_size - coreboot->header->header_bytes;
	found = FALSE;
	found_base = 0U;
	found_size = 0U;
	for (index = 0; index < coreboot->record_count; index++) {
		if (remaining < sizeof(struct cb_record)) {
			return EFI_COMPROMISED_DATA;
		}

		record = (const struct cb_record *)(const void *)cursor;
		if (record->size < sizeof(struct cb_record) || record->size > remaining) {
			return EFI_COMPROMISED_DATA;
		}

		if (record->tag == CB_TAG_CBMEM_ENTRY) {
			if (record->size < sizeof(*entry)) {
				return EFI_COMPROMISED_DATA;
			}

			entry = (const struct cb_cbmem_entry *)record;
			if (entry->id == id) {
				if (found)
					return EFI_COMPROMISED_DATA;
				found_base = (EFI_PHYSICAL_ADDRESS)entry->address.lo |
					((EFI_PHYSICAL_ADDRESS)entry->address.hi << 32);
				found_size = entry->entry_size;
				if (found_base == 0 || found_size < minimum_size ||
				    found_base > MAX_UINT64 - found_size) {
					return EFI_COMPROMISED_DATA;
				}
				found = TRUE;
			}
		}

		cursor += record->size;
		remaining -= record->size;
	}
	if (found) {
		*base = found_base;
		if (size != NULL)
			*size = found_size;
		return EFI_SUCCESS;
	}

	return EFI_NOT_FOUND;
}

static EFI_STATUS cdk2_coreboot_find_cbmem_entry(
	const struct cdk2_coreboot_handoff *coreboot, UINT32 id,
	UINT32 minimum_size, EFI_PHYSICAL_ADDRESS *base, UINT32 *size)
{
	EFI_STATUS status;

	if (coreboot == NULL || base == NULL) {
		return EFI_INVALID_PARAMETER;
	}
	if (coreboot->header == NULL) {
		return cdk2_coreboot_find_legacy_cbmem_entry(coreboot, id,
			minimum_size, base, size);
	}
	if (coreboot->header->header_bytes > coreboot->table_size) {
		return EFI_INVALID_PARAMETER;
	}

	status = cdk2_coreboot_find_unique_modern_cbmem_entry(coreboot, id,
		minimum_size, base, size);
	if (status == EFI_NOT_FOUND) {
		return cdk2_coreboot_find_legacy_cbmem_entry(coreboot, id,
			minimum_size, base, size);
	}
	return status;
}

static EFI_STATUS cdk2_coreboot_resolve_deadline_tsc_info(
	const struct cdk2_coreboot_handoff *coreboot,
	struct cdk2_deadline_tsc_info *info)
{
	const struct cdk2_coreboot_memory_range *range;
	const struct cdk2_diag_timestamp_table *timestamps;
	const struct cb_tsc_info *tsc_info;
	EFI_PHYSICAL_ADDRESS timestamp_base;
	UINT64 timestamp_end;
	UINT64 range_end;
	const void *record;
	UINT32 timestamp_size;
	UINTN capacity;
	UINTN owners;
	UINTN index;
	EFI_STATUS status;

	if (coreboot == NULL || info == NULL)
		return EFI_INVALID_PARAMETER;
	*info = (struct cdk2_deadline_tsc_info) {
		.revision = CDK2_DEADLINE_TSC_INFO_REVISION,
		.length = sizeof(*info),
	};

	/* A malformed or duplicate constant-rate claim is terminal.  It must not
	 * silently downgrade into the measured fallback. */
	if (coreboot->header == NULL) {
		status = EFI_NOT_FOUND;
	} else {
		status = cdk2_coreboot_find_unique_record(coreboot, CB_TAG_TSC_INFO,
			CDK2_COREBOOT_TSC_INFO_MIN_SIZE, &record);
	}
	if (status == EFI_SUCCESS) {
		tsc_info = record;
		if (tsc_info->freq_khz == 0U)
			return EFI_COMPROMISED_DATA;
		info->source = CDK2_DEADLINE_TSC_SOURCE_COREBOOT_CONSTANT_TSC;
		info->frequency_hz = (UINT64)tsc_info->freq_khz * 1000U;
		return EFI_SUCCESS;
	}
	if (status != EFI_NOT_FOUND)
		return status;

	if (coreboot->header == NULL) {
		status = EFI_NOT_FOUND;
	} else {
		status = cdk2_coreboot_find_unique_modern_cbmem_entry(coreboot,
			CBMEM_ID_TIMESTAMP,
			sizeof(struct cdk2_diag_timestamp_table),
			&timestamp_base, &timestamp_size);
	}
	if (status == EFI_NOT_FOUND)
		status = cdk2_coreboot_find_unique_legacy_deadline(coreboot,
			&timestamp_base, &timestamp_size);
	if (EFI_ERROR(status))
		return status;
	if (timestamp_base > MAX_UINTN ||
	    (timestamp_base & (sizeof(UINT64) - 1U)) != 0U ||
	    timestamp_base > MAX_UINT64 - timestamp_size)
		return EFI_COMPROMISED_DATA;
	timestamp_end = timestamp_base + timestamp_size;

	owners = 0U;
	for (index = 0U; index < coreboot->memory_range_count; index++) {
		range = &coreboot->memory_ranges[index];
		if (range->size == 0U || range->base > MAX_UINT64 - range->size)
			return EFI_COMPROMISED_DATA;
		range_end = range->base + range->size;
		if (timestamp_base >= range_end || range->base >= timestamp_end)
			continue;
		if (range->type != CB_MEM_TABLE || timestamp_base < range->base ||
		    timestamp_end > range_end)
			return EFI_SECURITY_VIOLATION;
		owners++;
	}
	if (owners != 1U)
		return EFI_SECURITY_VIOLATION;

	timestamps = (const void *)(UINTN)timestamp_base;
	capacity = (timestamp_size - sizeof(*timestamps)) /
		sizeof(timestamps->entries[0]);
	if (timestamps->max_entries > capacity ||
	    timestamps->num_entries > timestamps->max_entries)
		return EFI_COMPROMISED_DATA;
	if (timestamps->tick_freq_mhz == 0U)
		return EFI_NOT_FOUND;
	info->source = CDK2_DEADLINE_TSC_SOURCE_COREBOOT_CBMEM_MEASURED_TSC;
	info->frequency_hz = (UINT64)timestamps->tick_freq_mhz * 1000000U;
	return EFI_SUCCESS;
}

static EFI_STATUS
cdk2_coreboot_append_deadline_tsc_info_hob(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
	const struct cdk2_coreboot_handoff *coreboot)
{
	struct cdk2_deadline_tsc_info info;
	EFI_STATUS status;

	status = cdk2_coreboot_resolve_deadline_tsc_info(coreboot, &info);
	if (EFI_ERROR(status))
		return status;
	return cdk2_coreboot_append_guid_hob(handoff,
		&m_cdk2_deadline_tsc_info_guid, &info, sizeof(info));
}

#if CONFIG_CDK2_SPI_CONSOLE
static EFI_STATUS cdk2_coreboot_validate_spi_allocation(
	const struct cdk2_coreboot_handoff *coreboot,
	EFI_PHYSICAL_ADDRESS base, UINT32 size)
{
	const UINT8 *cursor;
	UINT64 end = base + size;
	UINTN remaining;
	BOOLEAN covered = FALSE;

	for (UINTN index = 0U; index < coreboot->memory_range_count; index++) {
		const struct cdk2_coreboot_memory_range *range =
			&coreboot->memory_ranges[index];
		UINT64 range_end = range->base + range->size;

		if (range->type == CB_MEM_TABLE && range_end >= range->base &&
		    base >= range->base && end <= range_end)
			covered = TRUE;
	}
	if (!covered || coreboot->header == NULL)
		return EFI_COMPROMISED_DATA;

	cursor = (const UINT8 *)coreboot->header + coreboot->header->header_bytes;
	remaining = coreboot->table_size - coreboot->header->header_bytes;
	for (UINTN index = 0U; index < coreboot->record_count; index++) {
		const struct cb_record *record = (const void *)cursor;

		if (remaining < sizeof(*record) || record->size < sizeof(*record) ||
		    record->size > remaining)
			return EFI_COMPROMISED_DATA;
		if (record->tag == CB_TAG_CBMEM_ENTRY &&
		    record->size >= sizeof(struct cb_cbmem_entry)) {
			const struct cb_cbmem_entry *entry = (const void *)record;
			UINT64 entry_base = entry->address.lo |
				((UINT64)entry->address.hi << 32);
			UINT64 entry_end = entry_base + entry->entry_size;

			if (entry->id != CBMEM_ID_PAYLOAD_SPI_CONSOLE &&
			    entry_end >= entry_base && entry_base < end && entry_end > base)
				return EFI_COMPROMISED_DATA;
		}
		cursor += record->size;
		remaining -= record->size;
	}
	return EFI_SUCCESS;
}
#endif

#if CONFIG_CDK2_CBMEM_CONSOLE || defined(CDK2_COREBOOT_BACKEND_TEST)
static EFI_STATUS cdk2_coreboot_find_cbmem_console(
	const struct cdk2_coreboot_handoff *coreboot,
	EFI_PHYSICAL_ADDRESS *base, UINT32 *size)
{
	const struct cdk2_coreboot_memory_range *range;
	const struct cdk2_cbmem_console *console;
	EFI_PHYSICAL_ADDRESS console_base;
	UINT64 console_end;
	UINT64 range_end;
	UINT32 console_bytes;
	UINT32 cursor;
	UINT32 body_size;
	UINTN index;
	EFI_STATUS status;
	BOOLEAN covered;

	if (coreboot == NULL || base == NULL || size == NULL)
		return EFI_INVALID_PARAMETER;

	/* coreboot owns this allocation and its lifetime.  CDK2 only appends to
	 * it after both the checksummed directory entry and CB_MEM_TABLE range
	 * prove the complete writable extent. */
	status = cdk2_coreboot_find_cbmem_entry(coreboot, CBMEM_ID_CONSOLE,
		sizeof(*console) + 1U, &console_base, &console_bytes);
	if (EFI_ERROR(status))
		return status;
	if (console_base > MAX_UINTN ||
	    (console_base & (sizeof(UINT32) - 1U)) != 0U ||
	    console_bytes > sizeof(*console) + CDK2_CBMEM_CONSOLE_MAX_SIZE)
		return EFI_COMPROMISED_DATA;

	console_end = console_base + console_bytes;
	covered = FALSE;
	for (index = 0; index < coreboot->memory_range_count; index++) {
		range = &coreboot->memory_ranges[index];
		if (range->type != CB_MEM_TABLE || range->base > MAX_UINT64 - range->size)
			continue;
		range_end = range->base + range->size;
		if (console_base >= range->base && console_end <= range_end) {
			covered = TRUE;
			break;
		}
	}
	if (!covered)
		return EFI_SECURITY_VIOLATION;

	console = (const struct cdk2_cbmem_console *)(UINTN)console_base;
	body_size = __atomic_load_n(&console->size, __ATOMIC_ACQUIRE);
	cursor = __atomic_load_n(&console->cursor, __ATOMIC_ACQUIRE);
	if (body_size == 0U || body_size > CDK2_CBMEM_CONSOLE_CURSOR_MASK ||
	    body_size != console_bytes - sizeof(*console) ||
	    (cursor & CDK2_CBMEM_CONSOLE_CURSOR_MASK) > body_size)
		return EFI_COMPROMISED_DATA;

	*base = console_base;
	*size = console_bytes;
	return EFI_SUCCESS;
}
#endif

static EFI_STATUS cdk2_coreboot_validate_smbios_table_range(
	EFI_PHYSICAL_ADDRESS region_base, UINT32 region_size,
	EFI_PHYSICAL_ADDRESS table_base, UINT32 table_size)
{
	EFI_PHYSICAL_ADDRESS region_end;
	EFI_PHYSICAL_ADDRESS table_end;

	if (table_base == 0U || table_size == 0U ||
	    region_base > MAX_UINT64 - region_size ||
	    table_base > MAX_UINT64 - table_size) {
		return EFI_COMPROMISED_DATA;
	}

	region_end = region_base + region_size;
	table_end = table_base + table_size;
	if (table_base < region_base || table_end > region_end) {
		return EFI_COMPROMISED_DATA;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS cdk2_coreboot_validate_smbios2_entry(
	const UINT8 *entry, EFI_PHYSICAL_ADDRESS region_base, UINT32 region_size)
{
	EFI_PHYSICAL_ADDRESS table_base;
	UINT32 table_size;

	if (entry[5] != CDK2_COREBOOT_SMBIOS2_ENTRY_SIZE || entry[6] < 2U ||
	    !cdk2_coreboot_bytes_equal(entry + CDK2_COREBOOT_SMBIOS2_INTERMEDIATE_OFFSET,
		m_cdk2_smbios2_intermediate_anchor,
		sizeof(m_cdk2_smbios2_intermediate_anchor)) ||
	    cdk2_coreboot_checksum8(entry, CDK2_COREBOOT_SMBIOS2_ENTRY_SIZE) != 0U ||
	    cdk2_coreboot_checksum8(entry + CDK2_COREBOOT_SMBIOS2_INTERMEDIATE_OFFSET,
		CDK2_COREBOOT_SMBIOS2_ENTRY_SIZE -
		CDK2_COREBOOT_SMBIOS2_INTERMEDIATE_OFFSET) != 0U) {
		return EFI_COMPROMISED_DATA;
	}

	table_base = cdk2_coreboot_read32(
		entry + CDK2_COREBOOT_SMBIOS2_TABLE_ADDRESS_OFFSET);
	table_size = cdk2_coreboot_read16(
		entry + CDK2_COREBOOT_SMBIOS2_TABLE_LENGTH_OFFSET);
	return cdk2_coreboot_validate_smbios_table_range(region_base, region_size,
		table_base, table_size);
}

static EFI_STATUS cdk2_coreboot_validate_smbios3_entry(
	const UINT8 *entry, EFI_PHYSICAL_ADDRESS region_base, UINT32 region_size)
{
	EFI_PHYSICAL_ADDRESS table_base;
	UINT32 table_size;

	if (entry[6] != CDK2_COREBOOT_SMBIOS3_ENTRY_SIZE || entry[7] < 3U ||
	    cdk2_coreboot_checksum8(entry, CDK2_COREBOOT_SMBIOS3_ENTRY_SIZE) != 0U) {
		return EFI_COMPROMISED_DATA;
	}

	table_base = cdk2_coreboot_read64(
		entry + CDK2_COREBOOT_SMBIOS3_TABLE_ADDRESS_OFFSET);
	table_size = cdk2_coreboot_read32(
		entry + CDK2_COREBOOT_SMBIOS3_TABLE_LENGTH_OFFSET);
	return cdk2_coreboot_validate_smbios_table_range(region_base, region_size,
		table_base, table_size);
}

static EFI_STATUS cdk2_coreboot_find_smbios_entries(
	EFI_PHYSICAL_ADDRESS region_base, UINT32 region_size,
	EFI_PHYSICAL_ADDRESS *smbios2_entry, EFI_PHYSICAL_ADDRESS *smbios3_entry)
{
	EFI_PHYSICAL_ADDRESS region_end;
	UINTN offset;
	EFI_STATUS status;

	if (smbios2_entry == NULL || smbios3_entry == NULL || region_base == 0U ||
	    region_size < CDK2_COREBOOT_SMBIOS3_ENTRY_SIZE || region_base > MAX_UINTN ||
	    region_base > MAX_UINT64 - region_size ||
	    region_base + region_size - 1U > MAX_UINTN) {
		return EFI_COMPROMISED_DATA;
	}

	*smbios2_entry = 0U;
	*smbios3_entry = 0U;
	region_end = region_base + region_size;
	offset = (CDK2_COREBOOT_SMBIOS_ENTRY_ALIGNMENT -
		(region_base & (CDK2_COREBOOT_SMBIOS_ENTRY_ALIGNMENT - 1U))) &
		(CDK2_COREBOOT_SMBIOS_ENTRY_ALIGNMENT - 1U);
	for (; offset < region_size;) {
		EFI_PHYSICAL_ADDRESS address = region_base + offset;
		const UINT8 *entry = (const UINT8 *)(UINTN)address;
		UINTN remaining = region_size - offset;

		if (remaining >= sizeof(m_cdk2_smbios3_anchor) &&
		    cdk2_coreboot_bytes_equal(entry, m_cdk2_smbios3_anchor,
			    sizeof(m_cdk2_smbios3_anchor))) {
			if (*smbios3_entry != 0U ||
			    remaining < CDK2_COREBOOT_SMBIOS3_ENTRY_SIZE) {
				return EFI_COMPROMISED_DATA;
			}
			status = cdk2_coreboot_validate_smbios3_entry(entry, region_base,
				region_size);
			if (EFI_ERROR(status)) {
				return status;
			}
			*smbios3_entry = address;
		} else if (remaining >= sizeof(m_cdk2_smbios2_anchor) &&
			   cdk2_coreboot_bytes_equal(entry, m_cdk2_smbios2_anchor,
				   sizeof(m_cdk2_smbios2_anchor))) {
			if (*smbios2_entry != 0U ||
			    remaining < CDK2_COREBOOT_SMBIOS2_ENTRY_SIZE) {
				return EFI_COMPROMISED_DATA;
			}
			status = cdk2_coreboot_validate_smbios2_entry(entry, region_base,
				region_size);
			if (EFI_ERROR(status)) {
				return status;
			}
			*smbios2_entry = address;
		}

		if (region_end - address <= CDK2_COREBOOT_SMBIOS_ENTRY_ALIGNMENT) {
			break;
		}
		offset += CDK2_COREBOOT_SMBIOS_ENTRY_ALIGNMENT;
	}

	if (*smbios2_entry == 0U && *smbios3_entry == 0U) {
		return EFI_COMPROMISED_DATA;
	}
	if (*smbios2_entry != 0U && *smbios3_entry != 0U) {
		const UINT8 *smbios2 = (const UINT8 *)(UINTN)*smbios2_entry;
		const UINT8 *smbios3 = (const UINT8 *)(UINTN)*smbios3_entry;

		if (cdk2_coreboot_read32(smbios2 +
			CDK2_COREBOOT_SMBIOS2_TABLE_ADDRESS_OFFSET) !=
			cdk2_coreboot_read64(smbios3 +
			CDK2_COREBOOT_SMBIOS3_TABLE_ADDRESS_OFFSET) ||
		    cdk2_coreboot_read16(smbios2 +
			CDK2_COREBOOT_SMBIOS2_TABLE_LENGTH_OFFSET) !=
			cdk2_coreboot_read32(smbios3 +
			CDK2_COREBOOT_SMBIOS3_TABLE_LENGTH_OFFSET)) {
			return EFI_COMPROMISED_DATA;
		}
	}

	return EFI_SUCCESS;
}

static EFI_STATUS cdk2_coreboot_append_smbios_hob(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
						  const struct cdk2_coreboot_handoff *coreboot)
{
	CDK2_SMBIOS_TABLE_HOB_V3 smbios_table;
	const EFI_GUID *smbios_guid;
	EFI_PHYSICAL_ADDRESS smbios_base;
	EFI_PHYSICAL_ADDRESS smbios2_entry;
	EFI_PHYSICAL_ADDRESS smbios3_entry;
	UINT32 smbios_size;
	EFI_STATUS status;

	status = cdk2_coreboot_find_cbmem_entry(coreboot, CBMEM_ID_SMBIOS,
						CDK2_COREBOOT_SMBIOS3_ENTRY_SIZE,
						&smbios_base, &smbios_size);
	if (status == EFI_NOT_FOUND) {
		return EFI_SUCCESS;
	}

	if (EFI_ERROR(status)) {
		return status;
	}

	smbios_table = (CDK2_SMBIOS_TABLE_HOB_V3) { 0 };
	smbios_table.header.revision = CDK2_SMBIOS_TABLE_HOB_V3_REVISION;
	smbios_table.header.length = sizeof(smbios_table);
	smbios_table.smbios_region_base = smbios_base;
	smbios_table.smbios_region_size = smbios_size;
	status = cdk2_coreboot_find_smbios_entries(smbios_base, smbios_size,
		&smbios2_entry, &smbios3_entry);
	if (EFI_ERROR(status)) {
		return status;
	}
	smbios_table.smbios2_entry_point = smbios2_entry;
	smbios_table.smbios3_entry_point = smbios3_entry;

	smbios_guid = smbios_table.smbios3_entry_point != 0U ?
		&m_cdk2_smbios3_table_guid : &m_cdk2_smbios_table_guid;
	return cdk2_coreboot_append_guid_hob(handoff, smbios_guid, &smbios_table,
					     sizeof(smbios_table));
}

#if CONFIG_CDK2_CAPSULE || defined(CDK2_COREBOOT_BACKEND_TEST)
static EFI_STATUS
cdk2_coreboot_append_capsule_hobs(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
				  const struct cdk2_coreboot_handoff *coreboot)
{
	const struct cb_record *record;
	const struct cb_range *range;
	const UINT8 *cursor;
	UINTN remaining;
	UINTN index;
	EFI_STATUS status;

	if (coreboot == NULL || coreboot->header == NULL ||
	    coreboot->header->header_bytes > coreboot->table_size) {
		return EFI_INVALID_PARAMETER;
	}

	cursor = (const UINT8 *)coreboot->header + coreboot->header->header_bytes;
	remaining = coreboot->table_size - coreboot->header->header_bytes;
	for (index = 0; index < coreboot->record_count; index++) {
		if (remaining < sizeof(struct cb_record)) {
			return EFI_COMPROMISED_DATA;
		}

		record = (const struct cb_record *)(const void *)cursor;
		if (record->size < sizeof(struct cb_record) || record->size > remaining) {
			return EFI_COMPROMISED_DATA;
		}

		if (record->tag == CB_TAG_CAPSULE) {
			if (record->size < sizeof(*range)) {
				return EFI_COMPROMISED_DATA;
			}

			range = (const struct cb_range *)record;
			if (range->range_start == 0 || range->range_size == 0 ||
			    range->range_start > MAX_UINT64 - range->range_size) {
				return EFI_COMPROMISED_DATA;
			}

			status = cdk2_coreboot_append_capsule_hob(handoff, range->range_start,
								  range->range_size);
			if (EFI_ERROR(status)) {
				return status;
			}
		}

		cursor += record->size;
		remaining -= record->size;
	}

	/* The checked table-size contract is exact: accepting a tail here would
	 * let a stale record count hide bytes from the capsule HOB walk. */
	return remaining == 0U ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}
#endif

static CFR_VARBINARY *cdk2_coreboot_cfr_extract_var_binary(UINT8 *buffer, UINTN *offset,
							   UINTN buffer_size, UINT32 target_tag)
{
	CFR_VARBINARY *var_binary;
	UINTN required_size;

	if (buffer == NULL || offset == NULL || *offset > buffer_size ||
	    buffer_size - *offset < sizeof(*var_binary)) {
		return NULL;
	}

	var_binary = (CFR_VARBINARY *)(buffer + *offset);
	if (var_binary->tag != target_tag) {
		return NULL;
	}

	if (var_binary->size < sizeof(*var_binary) || var_binary->size > buffer_size - *offset ||
	    var_binary->data_length > var_binary->size - sizeof(*var_binary)) {
		return NULL;
	}

	required_size = (sizeof(*var_binary) + var_binary->data_length + 3U) & ~(UINTN)3U;
	if (var_binary->size != required_size) {
		return NULL;
	}

	if (target_tag == CB_TAG_CFR_DEP_VALUES) {
		if ((var_binary->data_length % sizeof(UINT32)) != 0) {
			return NULL;
		}
	} else if (var_binary->data_length == 0 ||
		   var_binary->data[var_binary->data_length - 1] != '\0') {
		return NULL;
	}

	*offset += var_binary->size;
	return var_binary;
}

static EFI_STATUS cdk2_coreboot_cfr_validate_var_binary(const UINT8 *buffer, UINTN *offset,
							UINTN buffer_size, UINT32 tag)
{
	if (cdk2_coreboot_cfr_extract_var_binary((UINT8 *)buffer, offset, buffer_size, tag) ==
	    NULL) {
		return EFI_COMPROMISED_DATA;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS cdk2_coreboot_cfr_validate_object(const UINT8 *buffer, UINTN buffer_size,
						    UINTN depth)
{
	const CFR_OPTION_FORM *header;
	const CFR_OPTION_NUMERIC *numeric;
	const CFR_ENUM_VALUE *enum_value;
	const CFR_RUNTIME_APPLY *runtime_apply;
	UINTN offset;
	UINTN enum_offset;
	EFI_STATUS status;

	if (depth > CDK2_COREBOOT_CFR_MAX_DEPTH || buffer_size < sizeof(*header)) {
		return EFI_COMPROMISED_DATA;
	}

	header = (const CFR_OPTION_FORM *)buffer;
	if (header->size < sizeof(*header) || header->size > buffer_size ||
	    (header->flags & ~CDK2_COREBOOT_CFR_OPTION_FLAGS_MASK) != 0) {
		return EFI_COMPROMISED_DATA;
	}

	switch (header->tag) {
	case CB_TAG_CFR_OPTION_FORM:
		offset = sizeof(CFR_OPTION_FORM);
		status = cdk2_coreboot_cfr_validate_var_binary(buffer, &offset, header->size,
							       CB_TAG_CFR_VARCHAR_UI_NAME);
		if (EFI_ERROR(status)) {
			return status;
		}

		cdk2_coreboot_cfr_extract_var_binary((UINT8 *)buffer, &offset, header->size,
						     CB_TAG_CFR_DEP_VALUES);

		while (offset < header->size) {
			status = cdk2_coreboot_cfr_validate_object(
				buffer + offset, header->size - offset, depth + 1);
			if (EFI_ERROR(status)) {
				return status;
			}

			offset += ((const CFR_OPTION_FORM *)(buffer + offset))->size;
		}

		return (offset == header->size) ? EFI_SUCCESS : EFI_COMPROMISED_DATA;

	case CB_TAG_CFR_OPTION_ENUM:
	case CB_TAG_CFR_OPTION_NUMBER:
	case CB_TAG_CFR_OPTION_BOOL:
		if (header->size < sizeof(CFR_OPTION_NUMERIC)) {
			return EFI_COMPROMISED_DATA;
		}

		numeric = (const CFR_OPTION_NUMERIC *)buffer;
		offset = sizeof(CFR_OPTION_NUMERIC);
		status = cdk2_coreboot_cfr_validate_var_binary(buffer, &offset, header->size,
							       CB_TAG_CFR_VARCHAR_OPT_NAME);
		if (EFI_ERROR(status)) {
			return status;
		}

		status = cdk2_coreboot_cfr_validate_var_binary(buffer, &offset, header->size,
							       CB_TAG_CFR_VARCHAR_UI_NAME);
		if (EFI_ERROR(status)) {
			return status;
		}

		cdk2_coreboot_cfr_extract_var_binary((UINT8 *)buffer, &offset, header->size,
						     CB_TAG_CFR_VARCHAR_UI_HELPTEXT);
		cdk2_coreboot_cfr_extract_var_binary((UINT8 *)buffer, &offset, header->size,
						     CB_TAG_CFR_DEP_VALUES);

		if (header->size - offset >= sizeof(CFR_RUNTIME_APPLY)) {
			runtime_apply = (const CFR_RUNTIME_APPLY *)(buffer + offset);
			if (runtime_apply->tag == CB_TAG_CFR_RUNTIME_APPLY) {
				if (runtime_apply->size != sizeof(CFR_RUNTIME_APPLY)) {
					return EFI_COMPROMISED_DATA;
				}

				offset += runtime_apply->size;
			}
		}

		if (numeric->tag != CB_TAG_CFR_OPTION_ENUM) {
			return (offset == header->size) ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
		}

		while (offset < header->size) {
			if (header->size - offset < sizeof(CFR_ENUM_VALUE)) {
				return EFI_COMPROMISED_DATA;
			}

			enum_value = (const CFR_ENUM_VALUE *)(buffer + offset);
			if (enum_value->tag != CB_TAG_CFR_ENUM_VALUE ||
			    enum_value->size < sizeof(CFR_ENUM_VALUE) ||
			    enum_value->size > header->size - offset) {
				return EFI_COMPROMISED_DATA;
			}

			enum_offset = sizeof(CFR_ENUM_VALUE);
			status = cdk2_coreboot_cfr_validate_var_binary(
				(const UINT8 *)enum_value, &enum_offset, enum_value->size,
				CB_TAG_CFR_VARCHAR_UI_NAME);
			if (EFI_ERROR(status) || enum_offset != enum_value->size) {
				return EFI_COMPROMISED_DATA;
			}

			offset += enum_value->size;
		}

		return EFI_SUCCESS;

	case CB_TAG_CFR_OPTION_VARCHAR:
		offset = sizeof(CFR_OPTION_VARCHAR);
		status = cdk2_coreboot_cfr_validate_var_binary(buffer, &offset, header->size,
							       CB_TAG_CFR_VARCHAR_DEF_VALUE);
		if (EFI_ERROR(status)) {
			return status;
		}

		status = cdk2_coreboot_cfr_validate_var_binary(buffer, &offset, header->size,
							       CB_TAG_CFR_VARCHAR_OPT_NAME);
		if (EFI_ERROR(status)) {
			return status;
		}

		status = cdk2_coreboot_cfr_validate_var_binary(buffer, &offset, header->size,
							       CB_TAG_CFR_VARCHAR_UI_NAME);
		if (EFI_ERROR(status)) {
			return status;
		}

		cdk2_coreboot_cfr_extract_var_binary((UINT8 *)buffer, &offset, header->size,
						     CB_TAG_CFR_VARCHAR_UI_HELPTEXT);
		cdk2_coreboot_cfr_extract_var_binary((UINT8 *)buffer, &offset, header->size,
						     CB_TAG_CFR_DEP_VALUES);
		return (offset == header->size) ? EFI_SUCCESS : EFI_COMPROMISED_DATA;

	case CB_TAG_CFR_OPTION_COMMENT:
		offset = sizeof(CFR_OPTION_COMMENT);
		status = cdk2_coreboot_cfr_validate_var_binary(buffer, &offset, header->size,
							       CB_TAG_CFR_VARCHAR_UI_NAME);
		if (EFI_ERROR(status)) {
			return status;
		}

		cdk2_coreboot_cfr_extract_var_binary((UINT8 *)buffer, &offset, header->size,
						     CB_TAG_CFR_VARCHAR_UI_HELPTEXT);
		cdk2_coreboot_cfr_extract_var_binary((UINT8 *)buffer, &offset, header->size,
						     CB_TAG_CFR_DEP_VALUES);
		return (offset == header->size) ? EFI_SUCCESS : EFI_COMPROMISED_DATA;

	default:
		return EFI_COMPROMISED_DATA;
	}
}

static EFI_STATUS cdk2_coreboot_cfr_validate_form(const CFR_OPTION_FORM *form, UINTN form_size)
{
	EFI_STATUS status;

	if (form == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	status = cdk2_coreboot_cfr_validate_object((const UINT8 *)form, form_size, 0);
	if (EFI_ERROR(status) || form->tag != CB_TAG_CFR_OPTION_FORM ||
	    form->size != form_size) {
		return EFI_COMPROMISED_DATA;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS cdk2_coreboot_append_cfr_hobs(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
						const struct cdk2_coreboot_handoff *coreboot)
{
	const void *record;
	const struct cb_cfr *root;
	const CFR_OPTION_FORM *form;
	UINTN processed_length;
	EFI_STATUS status;

	status = cdk2_coreboot_find_record(coreboot, CB_TAG_CFR_ROOT, sizeof(*root), &record);
	if (status == EFI_NOT_FOUND) {
		return EFI_SUCCESS;
	}

	if (EFI_ERROR(status)) {
		return status;
	}

	root = (const struct cb_cfr *)record;
	if (root->version != CB_CFR_VERSION || root->size < sizeof(*root) ||
	    cdk2_coreboot_crc32(root + 1, root->size - sizeof(*root)) != root->checksum) {
		return EFI_COMPROMISED_DATA;
	}

	processed_length = sizeof(*root);
	while (processed_length < root->size) {
		if (root->size - processed_length < sizeof(*form)) {
			return EFI_COMPROMISED_DATA;
		}

		form = (const CFR_OPTION_FORM *)((const UINT8 *)root + processed_length);
		if (form->size > root->size - processed_length) {
			return EFI_COMPROMISED_DATA;
		}

		status = cdk2_coreboot_cfr_validate_form(form, form->size);
		if (EFI_ERROR(status)) {
			return EFI_COMPROMISED_DATA;
		}

		status = cdk2_coreboot_append_guid_hob(handoff, &m_cdk2_cfr_setup_menu_form_guid,
						       form, form->size);
		if (EFI_ERROR(status)) {
			return status;
		}

		processed_length += form->size;
	}

	return EFI_SUCCESS;
}

struct cdk2_coreboot_hob_append_state {
	EFI_PHYSICAL_ADDRESS end_of_hob_list;
	EFI_PHYSICAL_ADDRESS free_memory_bottom;
	EFI_HOB_GENERIC_HEADER end_marker;
};

static UINT32 cdk2_coreboot_pixel_mask(UINT8 size, UINT8 position)
{
	if (size == 0 || position >= 32 || size > 32 - position) {
		return 0;
	}

	if (size == 32) {
		return MAX_UINT32;
	}

	return ((1U << size) - 1U) << position;
}

static void cdk2_coreboot_copy_bytes(void *destination, const void *source, UINTN length)
{
	UINT8 *destination_bytes;
	const UINT8 *source_bytes;
	UINTN index;

	destination_bytes = (UINT8 *)destination;
	source_bytes = (const UINT8 *)source;
	for (index = 0; index < length; index++) {
		destination_bytes[index] = source_bytes[index];
	}
}

struct cdk2_native_idt_gate {
	UINT16 offset_low;
	UINT16 selector;
	UINT8 ist;
	UINT8 attributes;
	UINT16 offset_middle;
	UINT32 offset_high;
	UINT32 reserved;
};

struct cdk2_native_idtr {
	UINT16 limit;
	UINTN base;
} __packed;

#if defined(__x86_64__) && !defined(CDK2_COREBOOT_BACKEND_TEST)
#if defined(__GNUC__)
static struct cdk2_native_idt_gate m_cdk2_native_idt[256] __aligned(16);
#else
static struct cdk2_native_idt_gate m_cdk2_native_idt[256];
#endif

#endif

#if defined(CDK2_COREBOOT_BACKEND_TEST)
void cdk2_coreboot_test_io_write8(UINT16 port, UINT8 value);
#endif

static void cdk2_coreboot_io_write8(UINT16 port, UINT8 value)
{
#if defined(__x86_64__) && !defined(CDK2_COREBOOT_BACKEND_TEST)
	__asm__ volatile("outb %0, %w1" : : "a"(value), "Nd"(port));
#elif defined(CDK2_COREBOOT_BACKEND_TEST)
	cdk2_coreboot_test_io_write8(port, value);
#else
	(void)port;
	(void)value;
#endif
}

static EFI_STATUS cdk2_coreboot_validate_framebuffer_geometry(
	const struct cb_framebuffer *framebuffer)
{
	UINT64 minimum_line_bits;
	UINT64 minimum_line_bytes;
	UINT64 stride_bits;

	if (framebuffer == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	if (framebuffer->bits_per_pixel == 0 || framebuffer->bits_per_pixel > 32 ||
	    framebuffer->x_resolution == 0 || framebuffer->y_resolution == 0 ||
	    framebuffer->bytes_per_line == 0 ||
	    framebuffer->bytes_per_line > MAX_UINT32 / framebuffer->y_resolution ||
	    framebuffer->bytes_per_line > MAX_UINT32 / 8U) {
		return EFI_COMPROMISED_DATA;
	}

	minimum_line_bits = (UINT64)framebuffer->x_resolution * framebuffer->bits_per_pixel;
	minimum_line_bytes = (minimum_line_bits + 7U) / 8U;
	if (minimum_line_bytes > framebuffer->bytes_per_line) {
		return EFI_COMPROMISED_DATA;
	}

	stride_bits = (UINT64)framebuffer->bytes_per_line * 8U;
	if ((stride_bits % framebuffer->bits_per_pixel) != 0) {
		return EFI_COMPROMISED_DATA;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS cdk2_coreboot_fill_diagnostic_handoff(
	const struct cdk2_coreboot_handoff *coreboot,
	struct cdk2_diag_handoff *diagnostic)
{
#if CONFIG_CDK2_SERIAL
	const struct cb_serial *serial;
#endif
#if CONFIG_CDK2_SERIAL || CONFIG_CDK2_SPI_CONSOLE
	EFI_STATUS status;
#endif

	*diagnostic = (struct cdk2_diag_handoff){
		.revision = CDK2_DIAG_HANDOFF_REVISION,
		.length = sizeof(*diagnostic),
	};
	(void)coreboot;
#if CONFIG_CDK2_SERIAL
	const void *record;

	status = cdk2_coreboot_find_unique_record(coreboot, CB_TAG_SERIAL,
		CDK2_COREBOOT_SERIAL_MIN_SIZE, &record);
	if (!EFI_ERROR(status)) {
		serial = record;
		if ((serial->type == CB_SERIAL_TYPE_IO_MAPPED ||
		     serial->type == CB_SERIAL_TYPE_MEMORY_MAPPED) &&
		    cdk2_uart_registers_valid(serial->type == CB_SERIAL_TYPE_IO_MAPPED ?
			CDK2_DIAG_SERIAL_PIO : CDK2_DIAG_SERIAL_MMIO,
			serial->baseaddr, serial->regwidth, serial->regwidth, 5U)) {
			diagnostic->serial_type = serial->type == CB_SERIAL_TYPE_IO_MAPPED ?
				CDK2_DIAG_SERIAL_PIO : CDK2_DIAG_SERIAL_MMIO;
			diagnostic->register_width = (UINT8)serial->regwidth;
			diagnostic->register_stride = (UINT16)serial->regwidth;
			diagnostic->register_base = serial->baseaddr;
			diagnostic->baud_rate = serial->baud;
			if (serial->size >= CDK2_COREBOOT_RECORD_FIELD_END(
			    struct cb_serial, input_hertz))
				diagnostic->input_hertz = serial->input_hertz;
		}
	} else if (status != EFI_NOT_FOUND) {
		return status;
	}
#endif
#if CONFIG_CDK2_CBMEM_CONSOLE
	{
		EFI_PHYSICAL_ADDRESS base;
		UINT32 bytes;

		if (!EFI_ERROR(cdk2_coreboot_find_cbmem_console(coreboot, &base, &bytes))) {
			diagnostic->cbmem_console_base = base;
			diagnostic->cbmem_console_bytes = bytes;
		}
	}
#endif
#if CONFIG_CDK2_SPI_CONSOLE
	{
		const struct cb_payload_spi_console *spi;
		const void *record;
		EFI_PHYSICAL_ADDRESS allocation_base;
		UINT32 allocation_size;
		UINT64 end;

		status = cdk2_coreboot_find_record(coreboot,
			CB_TAG_PAYLOAD_SPI_CONSOLE, sizeof(*spi), &record);
		if (!EFI_ERROR(status)) {
			spi = record;
			end = spi->com_buffer + spi->com_buffer_size;
			if (spi->size != sizeof(*spi) || spi->version != 1U ||
			    spi->request_header_size != 16U || spi->com_buffer == 0U ||
			    (spi->com_buffer & 3U) != 0U ||
			    spi->com_buffer > MAX_UINTN || end < spi->com_buffer ||
			    end == 0U || end - 1U > MAX_UINTN ||
			    spi->com_buffer_size < spi->request_header_size ||
			    spi->max_chunk == 0U || spi->max_chunk > 256U ||
			    spi->max_chunk > spi->com_buffer_size -
				spi->request_header_size || spi->boot_limit < spi->max_chunk ||
			    spi->apm_cmd != 0xe8U || spi->reserved[0] != 0U ||
			    spi->reserved[1] != 0U || spi->reserved[2] != 0U)
				return EFI_SUCCESS;
			status = cdk2_coreboot_find_cbmem_entry(coreboot,
				CBMEM_ID_PAYLOAD_SPI_CONSOLE, spi->com_buffer_size,
				&allocation_base, &allocation_size);
			if (EFI_ERROR(status) || allocation_base != spi->com_buffer ||
			    allocation_size != spi->com_buffer_size)
				return EFI_SUCCESS;
			if (EFI_ERROR(cdk2_coreboot_validate_spi_allocation(coreboot,
			    allocation_base, allocation_size)))
				return EFI_SUCCESS;
			diagnostic->spi_console_buffer = spi->com_buffer;
			diagnostic->spi_console_buffer_bytes = spi->com_buffer_size;
			diagnostic->spi_console_max_chunk = spi->max_chunk;
			diagnostic->spi_console_boot_limit = spi->boot_limit;
			diagnostic->spi_console_apm_cmd = spi->apm_cmd;
			diagnostic->spi_console_remaining =
				m_diagnostic_spi_console_active ?
				m_diagnostic_spi_console_remaining : spi->boot_limit;
		} else if (status != EFI_NOT_FOUND) {
			return status;
		}
	}
#endif
	return EFI_SUCCESS;
}

/* Reserve the fixed physical mailbox independently of any selected variable driver. */
static EFI_STATUS cdk2_coreboot_append_authvar_mailbox_hob(
	EFI_HOB_HANDOFF_INFO_TABLE hob[], const struct cdk2_coreboot_handoff *coreboot)
{
	const struct cb_authvar_service_endpoint *endpoint;
	const void *record;
	UINT64 base;
	UINT64 end;
	UINT64 page_base;
	UINT64 page_end;
	UINTN owners = 0U;
	EFI_STATUS status;

	status = cdk2_coreboot_find_unique_record(coreboot,
		CB_TAG_AUTHVAR_SERVICE_ENDPOINT, sizeof(*endpoint), &record);
	if (EFI_ERROR(status))
		return status;
	endpoint = record;
	base = (UINT64)endpoint->communication_base.lo |
		((UINT64)endpoint->communication_base.hi << 32);
	if (EFI_ERROR(cdk2_authvar_service_endpoint_validate(endpoint)) ||
	    base > MAX_UINT64 - endpoint->communication_size ||
	    base + endpoint->communication_size > MAX_UINT64 - EFI_PAGE_MASK)
		return EFI_COMPROMISED_DATA;
	end = base + endpoint->communication_size;
	page_base = base & ~(UINT64)EFI_PAGE_MASK;
	page_end = (end + EFI_PAGE_MASK) & ~(UINT64)EFI_PAGE_MASK;
	for (UINTN index = 0; index < coreboot->memory_range_count; index++) {
		const struct cdk2_coreboot_memory_range *range =
			&coreboot->memory_ranges[index];
		UINT64 range_end;

		if (!range->size || range->base > MAX_UINT64 - range->size)
			return EFI_COMPROMISED_DATA;
		range_end = range->base + range->size;
		if (page_base < range_end && range->base < page_end) {
			if (range->type != CB_MEM_TABLE || page_base < range->base ||
			    page_end > range_end)
				return EFI_COMPROMISED_DATA;
			owners++;
		}
	}
	if (owners != 1U)
		return EFI_COMPROMISED_DATA;
	return cdk2_coreboot_append_memory_allocation_hob(hob, page_base,
		page_end - page_base, efi_runtime_services_data);
}

static EFI_STATUS cdk2_coreboot_append_smmstore_hobs(
	EFI_HOB_HANDOFF_INFO_TABLE hob[], const struct cdk2_coreboot_handoff *coreboot)
{
	const struct cb_smmstorev2 *smm_store;
	SMMSTORE_INFO smm_store_info;
	CDK2_VARIABLE_FLASH_INFO flash_info;
	const void *record;
	EFI_STATUS status;
	UINT64 total_size;
	UINT64 flash_base;
	UINT64 spare_blocks;
	UINT64 communication_base;
	UINT64 communication_buffer;
	UINT64 communication_end;
	UINT64 communication_size;

	status = cdk2_coreboot_find_unique_record(coreboot, CB_TAG_SMMSTOREV2,
					   CDK2_COREBOOT_SMMSTOREV2_MIN_SIZE, &record);
	if (status == EFI_NOT_FOUND)
		return EFI_NOT_FOUND;
	if (EFI_ERROR(status))
		return status;

	smm_store = (const struct cb_smmstorev2 *)record;
	smm_store_info = (SMMSTORE_INFO){0};
	smm_store_info.com_buffer = smm_store->com_buffer;
	smm_store_info.com_buffer_size = smm_store->com_buffer_size;
	smm_store_info.num_blocks = smm_store->num_blocks;
	smm_store_info.block_size = smm_store->block_size;
	smm_store_info.mmio_address = smm_store->mmap_addr;
	smm_store_info.apm_cmd = smm_store->apm_cmd;
	communication_buffer = smm_store->com_buffer;
	communication_size = smm_store->com_buffer_size;
	communication_base = communication_buffer & ~(UINT64)EFI_PAGE_MASK;
	if (communication_buffer == 0U || communication_size == 0U ||
	    communication_buffer > MAX_UINT64 - communication_size ||
	    communication_buffer + communication_size > MAX_UINT64 - EFI_PAGE_MASK)
		return EFI_COMPROMISED_DATA;
	if (smm_store->block_size == 0U)
		return EFI_COMPROMISED_DATA;
	communication_end = (communication_buffer + communication_size + EFI_PAGE_MASK) &
		~(UINT64)EFI_PAGE_MASK;
	{
		UINTN owners = 0U;

		for (UINTN index = 0U; index < coreboot->memory_range_count; index++) {
			const struct cdk2_coreboot_memory_range *range =
				&coreboot->memory_ranges[index];
			UINT64 range_end;

			if (range->size == 0U || range->base > MAX_UINT64 - range->size)
				return EFI_COMPROMISED_DATA;
			range_end = range->base + range->size;
			if (communication_base < range_end && range->base < communication_end) {
				if (range->type != CB_MEM_TABLE ||
				    communication_base < range->base || communication_end > range_end)
					return EFI_COMPROMISED_DATA;
				owners++;
			}
		}
		if (owners != 1U)
			return EFI_COMPROMISED_DATA;
	}
	status = cdk2_coreboot_append_memory_allocation_hob(
		hob, communication_base, communication_end - communication_base,
		efi_runtime_services_data);
	if (EFI_ERROR(status))
		return status;
	status = cdk2_coreboot_append_guid_hob(hob, &m_cdk2_smm_store_info_hob_guid,
					       &smm_store_info, sizeof(smm_store_info));
	if (EFI_ERROR(status))
		return status;

	total_size = (UINT64)smm_store->num_blocks * smm_store->block_size;
	flash_base = smm_store->mmap_addr;
	if (flash_base == 0 && total_size <= 0x100000000ULL)
		flash_base = 0x100000000ULL - total_size;
	if (flash_base != 0 && flash_base > MAX_UINT64 - total_size)
		return EFI_COMPROMISED_DATA;
	spare_blocks = smm_store->num_blocks / 2U;
	if (flash_base != 0 && smm_store->num_blocks >= 4U &&
	    spare_blocks < smm_store->num_blocks) {
		flash_info = (CDK2_VARIABLE_FLASH_INFO){
			.variable_base = flash_base,
			.variable_length =
				(UINT64)(smm_store->num_blocks - spare_blocks - 1U) *
				smm_store->block_size,
			.spare_base = flash_base +
				(UINT64)(smm_store->num_blocks - spare_blocks) *
				smm_store->block_size,
			.spare_length = (UINT64)spare_blocks * smm_store->block_size,
			.working_base = flash_base +
				(UINT64)(smm_store->num_blocks - spare_blocks - 1U) *
				smm_store->block_size,
			.working_length = smm_store->block_size,
		};
		status = cdk2_coreboot_append_guid_hob(
			hob, &m_cdk2_variable_flash_info_hob_guid, &flash_info,
			sizeof(flash_info));
		if (EFI_ERROR(status))
			return status;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS cdk2_coreboot_append_serial_hobs(EFI_HOB_HANDOFF_INFO_TABLE *hob,
	const struct cdk2_coreboot_handoff *coreboot)
{
	const struct cb_serial *serial;
	const void *record;
	EFI_STATUS status;
#if CONFIG_CDK2_NATIVE_SERIAL_IO
	CDK2_SERIAL_PORT_HOB serial_info;
#else
	CDK2_SERIAL_PORT_HOB_V1 retained_serial_info;
#endif
	SERIAL_PORT_INFO legacy_serial_info;

	status = cdk2_coreboot_find_unique_record(coreboot, CB_TAG_SERIAL,
					   CDK2_COREBOOT_SERIAL_MIN_SIZE, &record);
	if (!EFI_ERROR(status)) {
		serial = (const struct cb_serial *)record;
		/* Serial is optional; never export a narrowed, invalid hardware tuple. */
		if ((serial->type != CB_SERIAL_TYPE_IO_MAPPED &&
		     serial->type != CB_SERIAL_TYPE_MEMORY_MAPPED) ||
		    !cdk2_uart_registers_valid(serial->type == CB_SERIAL_TYPE_IO_MAPPED ?
			CDK2_UART_PIO : CDK2_UART_MMIO, serial->baseaddr,
			serial->regwidth, serial->regwidth, 6U))
			status = EFI_NOT_FOUND;
	}
	if (!EFI_ERROR(status)) {
#if CONFIG_CDK2_NATIVE_SERIAL_IO
		serial_info = (CDK2_SERIAL_PORT_HOB) { 0 };
		serial_info.header.revision = CDK2_SERIAL_PORT_HOB_REVISION;
		serial_info.header.length = sizeof(serial_info);
		serial_info.use_mmio = (serial->type == CB_SERIAL_TYPE_IO_MAPPED) ? FALSE : TRUE;
		serial_info.register_stride = (UINT8)serial->regwidth;
		serial_info.baud_rate = serial->baud;
		if (serial->size >=
		    CDK2_COREBOOT_RECORD_FIELD_END(struct cb_serial, input_hertz) &&
		    serial->input_hertz != 0) {
			serial_info.input_hertz = serial->input_hertz;
		}
		serial_info.register_base = serial->baseaddr;
		status = cdk2_coreboot_append_guid_hob(hob, &m_cdk2_serial_port_info_guid,
						       &serial_info, sizeof(serial_info));
		if (EFI_ERROR(status)) {
			return status;
		}
#else
		retained_serial_info = (CDK2_SERIAL_PORT_HOB_V1) { 0 };
		retained_serial_info.header.revision = CDK2_SERIAL_PORT_HOB_V1_REVISION;
		retained_serial_info.header.length = sizeof(retained_serial_info);
		retained_serial_info.use_mmio =
			(serial->type == CB_SERIAL_TYPE_IO_MAPPED) ? FALSE : TRUE;
		retained_serial_info.register_stride = (UINT8)serial->regwidth;
		retained_serial_info.baud_rate = serial->baud;
		retained_serial_info.register_base = serial->baseaddr;
		status = cdk2_coreboot_append_guid_hob(hob, &m_cdk2_serial_port_info_guid,
			&retained_serial_info, sizeof(retained_serial_info));
		if (EFI_ERROR(status))
			return status;
#endif

		if (serial->size >=
			    CDK2_COREBOOT_RECORD_FIELD_END(struct cb_serial, input_hertz) &&
		    serial->input_hertz != 0) {
			legacy_serial_info = (SERIAL_PORT_INFO) {0};
			legacy_serial_info.revision = 1;
			legacy_serial_info.type = serial->type;
			legacy_serial_info.base_addr = serial->baseaddr;
			legacy_serial_info.baud = serial->baud;
			legacy_serial_info.reg_width = serial->regwidth;
			legacy_serial_info.input_hertz = serial->input_hertz;
			if (serial->size >=
			    CDK2_COREBOOT_RECORD_FIELD_END(struct cb_serial, uart_pci_addr)) {
				legacy_serial_info.uart_pci_addr = serial->uart_pci_addr;
			}

			status = cdk2_coreboot_append_guid_hob(hob,
							       &m_cdk2_legacy_serial_port_info_guid,
							       &legacy_serial_info,
							       sizeof(legacy_serial_info));
			if (EFI_ERROR(status)) {
				return status;
			}
		}
	} else if (status != EFI_NOT_FOUND) {
		return status;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI cdk2_coreboot_build_platform_hobs(struct cdk2_native_context *context,
							   void **handoff)
{
	UINT8 physical_address_bits;
	UINT32 maximum_function;
	UINT32 eax;
	EFI_STATUS status;
	const void *record;
	const struct cb_framebuffer *framebuffer;
	const struct lb_efi_fw_info *firmware;
	const struct cb_tpm_physical_presence *tpm_ppi;
	const struct cb_string *version;
	const struct cb_string *extra_version;
	struct cdk2_diag_handoff diagnostic_info;
	EFI_PEI_GRAPHICS_INFO_HOB graphics_info;
	EFI_HOB_HANDOFF_INFO_TABLE *hob;
	FIRMWARE_INFO firmware_info;
	TCG_PHYSICAL_PRESENCE_INFO tpm_ppi_info;
	UINT32 red_mask;
	UINT32 green_mask;
	UINT32 blue_mask;
	UINT32 reserved_mask;
	UINTN length;
	UINTN version_length;

	if (context == NULL || handoff == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	status = cdk2_coreboot_update_acpi_handoff(&m_coreboot_handoff);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = cdk2_coreboot_build_hobs(&m_coreboot_handoff, context->hob_memory_bottom,
					  context->hob_memory_top,
					  context->hob_free_memory_bottom,
					  context->hob_free_memory_top,
					  CONFIG_CDK2_CAPSULE != 0, (void **)&hob);
	if (EFI_ERROR(status)) {
		return status;
	}

	*handoff = hob;

#if CONFIG_CDK2_LINEAR_BOOT
	status = cdk2_linear_state_validate(&m_linear_state);
	if (EFI_ERROR(status) ||
	    m_linear_state.next_phase != CDK2_LINEAR_DXE_SERVICES)
		return EFI_COMPROMISED_DATA;
	status = cdk2_coreboot_append_guid_hob(hob,
		&cdk2_linear_state_hob_guid, &m_linear_state,
		sizeof(m_linear_state));
	if (EFI_ERROR(status))
		return status;
	if ((m_linear_state.flags & CDK2_LINEAR_STATE_COREBOOT_SPLASH) != 0U) {
		UINT64 splash_size = ALIGN_VALUE(m_linear_state.splash_bmp_size,
			EFI_PAGE_SIZE);

		if (splash_size == 0U || splash_size > MAX_UINT32 ||
		    m_linear_state.splash_bmp_address > MAX_UINT64 - splash_size)
			return EFI_COMPROMISED_DATA;
		status = cdk2_coreboot_append_memory_allocation_hob(
			hob, m_linear_state.splash_bmp_address, splash_size,
			efi_boot_services_data);
		if (EFI_ERROR(status))
			return status;
	}
#endif

	status = cdk2_coreboot_append_memory_allocation_hob(
		hob, context->payload_base, context->payload_size, efi_boot_services_data);
	if (EFI_ERROR(status)) {
		return status;
	}

	physical_address_bits = 36;
#if defined(__x86_64__)
	__asm__ volatile("cpuid" : "=a"(eax) : "a"(0x80000000U) : "rbx", "rcx", "rdx");
	maximum_function = eax;
	if (maximum_function >= 0x80000008U) {
		__asm__ volatile("cpuid" : "=a"(eax) : "a"(0x80000008U) : "rbx", "rcx", "rdx");
		physical_address_bits = (UINT8)(eax & 0xffU);
	}
#endif

	status = cdk2_coreboot_append_cpu_hob(hob, physical_address_bits, 16);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = cdk2_coreboot_append_coreboot_table_hob(hob, &m_coreboot_handoff);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = cdk2_coreboot_append_acpi_hobs(hob, &m_coreboot_handoff);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = cdk2_coreboot_append_smbios_hob(hob, &m_coreboot_handoff);
	if (EFI_ERROR(status)) {
		return status;
	}

	/*
	 * CFR backs the optional setup UI. Match the legacy ParseMiscInfo() path by
	 * continuing when the data is absent, unsupported, or malformed.
	 */
	(void)cdk2_coreboot_append_cfr_hobs(hob, &m_coreboot_handoff);

#if CONFIG_CDK2_CAPSULE
	status = cdk2_coreboot_append_capsule_hobs(hob, &m_coreboot_handoff);
	if (EFI_ERROR(status)) {
		return status;
	}
#endif

	status = cdk2_coreboot_append_serial_hobs(hob, &m_coreboot_handoff);
	if (EFI_ERROR(status))
		return status;
	status = cdk2_coreboot_fill_diagnostic_handoff(&m_coreboot_handoff,
		&diagnostic_info);
	if (EFI_ERROR(status))
		return status;
#if CONFIG_CDK2_CBMEM_TIMESTAMPS
	{
		EFI_PHYSICAL_ADDRESS timestamp_base;
		UINT32 timestamp_size;

		if (!EFI_ERROR(cdk2_coreboot_find_cbmem_entry(&m_coreboot_handoff,
		    CBMEM_ID_TIMESTAMP, sizeof(struct cdk2_diag_timestamp_table),
		    &timestamp_base, &timestamp_size))) {
			diagnostic_info.timestamp_base = timestamp_base;
			diagnostic_info.timestamp_bytes = timestamp_size;
		}
	}
#endif
	status = cdk2_coreboot_append_guid_hob(hob, &cdk2_diag_handoff_guid,
		&diagnostic_info, sizeof(diagnostic_info));
	if (EFI_ERROR(status))
		return status;

	status = cdk2_coreboot_find_unique_record(&m_coreboot_handoff, CB_TAG_FRAMEBUFFER,
						   CDK2_COREBOOT_FRAMEBUFFER_MIN_SIZE, &record);
	if (!EFI_ERROR(status)) {
		framebuffer = (const struct cb_framebuffer *)record;
		status = cdk2_coreboot_validate_framebuffer_geometry(framebuffer);
		if (EFI_ERROR(status)) {
			return status;
		}

		red_mask = cdk2_coreboot_pixel_mask(framebuffer->red_mask_size,
						   framebuffer->red_mask_pos);
		green_mask = cdk2_coreboot_pixel_mask(framebuffer->green_mask_size,
						     framebuffer->green_mask_pos);
		blue_mask = cdk2_coreboot_pixel_mask(framebuffer->blue_mask_size,
						    framebuffer->blue_mask_pos);
		reserved_mask = cdk2_coreboot_pixel_mask(framebuffer->reserved_mask_size,
							framebuffer->reserved_mask_pos);
		if ((framebuffer->red_mask_size != 0 && red_mask == 0) ||
		    (framebuffer->green_mask_size != 0 && green_mask == 0) ||
		    (framebuffer->blue_mask_size != 0 && blue_mask == 0) ||
		    (framebuffer->reserved_mask_size != 0 && reserved_mask == 0)) {
			return EFI_COMPROMISED_DATA;
		}

		graphics_info = (EFI_PEI_GRAPHICS_INFO_HOB){0};
		graphics_info.frame_buffer_base = cdk2_coreboot_framebuffer_address(framebuffer);
		graphics_info.frame_buffer_size =
			(UINT64)framebuffer->bytes_per_line * framebuffer->y_resolution;
		graphics_info.graphics_mode.version = 0;
		graphics_info.graphics_mode.horizontal_resolution = framebuffer->x_resolution;
		graphics_info.graphics_mode.vertical_resolution = framebuffer->y_resolution;
		graphics_info.graphics_mode.pixels_per_scan_line =
			(framebuffer->bytes_per_line * 8U) / framebuffer->bits_per_pixel;
		graphics_info.graphics_mode.pixel_information.red_mask = red_mask;
		graphics_info.graphics_mode.pixel_information.green_mask = green_mask;
		graphics_info.graphics_mode.pixel_information.blue_mask = blue_mask;
		graphics_info.graphics_mode.pixel_information.reserved_mask = reserved_mask;
		graphics_info.graphics_mode.pixel_format = pixel_bit_mask;
		if (framebuffer->bits_per_pixel == 32 && framebuffer->red_mask_size == 8 &&
		    framebuffer->green_mask_size == 8 && framebuffer->blue_mask_size == 8 &&
		    framebuffer->reserved_mask_size == 8 && framebuffer->red_mask_pos == 0 &&
		    framebuffer->green_mask_pos == 8 && framebuffer->blue_mask_pos == 16) {
			graphics_info.graphics_mode.pixel_format =
				pixel_red_green_blue_reserved8_bit_per_color;
		} else if (framebuffer->bits_per_pixel == 32 &&
			   framebuffer->red_mask_size == 8 &&
			   framebuffer->green_mask_size == 8 &&
			   framebuffer->blue_mask_size == 8 &&
			   framebuffer->reserved_mask_size == 8 &&
			   framebuffer->blue_mask_pos == 0 &&
			   framebuffer->green_mask_pos == 8 &&
			   framebuffer->red_mask_pos == 16) {
			graphics_info.graphics_mode.pixel_format =
				pixel_blue_green_red_reserved8_bit_per_color;
		}

		status = cdk2_coreboot_append_guid_hob(hob, &m_cdk2_graphics_info_hob_guid,
						       &graphics_info, sizeof(graphics_info));
		if (EFI_ERROR(status)) {
			return status;
		}
	} else if (status != EFI_NOT_FOUND) {
		return status;
	}

	status = cdk2_coreboot_append_authvar_mailbox_hob(hob, &m_coreboot_handoff);
	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND)
		return status;

	status = cdk2_coreboot_append_smmstore_hobs(hob, &m_coreboot_handoff);
	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND) {
		return status;
	}

	status = cdk2_coreboot_append_smram_hob(hob, &m_coreboot_handoff);
	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND)
		return status;

	status = cdk2_coreboot_append_smm_register_info_hob(hob, &m_coreboot_handoff);
	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND)
		return status;

	status = cdk2_coreboot_append_s3_communication_hob(hob,
		&m_coreboot_handoff);
	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND)
		return status;

#if CONFIG_CDK2_NATIVE_LOCAL_APIC_TIMER
	status = cdk2_coreboot_append_local_apic_timer_info_hob(hob,
		&m_coreboot_handoff);
	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND)
		return status;
#endif

	status = cdk2_coreboot_append_deadline_tsc_info_hob(hob,
		&m_coreboot_handoff);
	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND)
		return status;

	status = cdk2_coreboot_append_tsc_info_hob(hob, &m_coreboot_handoff);
	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND)
		return status;

	status = cdk2_coreboot_find_unique_record(&m_coreboot_handoff, CB_TAG_FW_INFO,
					   CDK2_COREBOOT_FW_INFO_MIN_SIZE, &record);
	if (!EFI_ERROR(status)) {
		firmware = (const struct lb_efi_fw_info *)record;
		firmware_info = (FIRMWARE_INFO){0};
		version_length = 0;
		cdk2_coreboot_copy_bytes(&firmware_info.type, firmware->guid,
					 sizeof(firmware_info.type));
		firmware_info.version = firmware->version;
		firmware_info.lowest_supported_version = firmware->lowest_supported_version;
		firmware_info.image_size = firmware->fw_size;

		status = cdk2_coreboot_find_record(&m_coreboot_handoff, CB_TAG_VERSION,
						   sizeof(struct cb_record) + 1,
						   (const void **)&version);
		if (!EFI_ERROR(status)) {
			length = cdk2_coreboot_string_record_length(version);
			if (length >= sizeof(firmware_info.version_str)) {
				length = sizeof(firmware_info.version_str) - 1;
			}

			cdk2_coreboot_copy_bytes(firmware_info.version_str, version->string,
						 length);
			version_length = length;
		} else if (status != EFI_NOT_FOUND) {
			return status;
		}

		status = cdk2_coreboot_find_record(&m_coreboot_handoff, CB_TAG_EXTRA_VERSION,
						   sizeof(struct cb_record) + 1,
						   (const void **)&extra_version);
		if (!EFI_ERROR(status)) {
			length = cdk2_coreboot_string_record_length(extra_version);
			if (length > sizeof(firmware_info.version_str) - 1 - version_length) {
				length = sizeof(firmware_info.version_str) - 1 - version_length;
			}

			cdk2_coreboot_copy_bytes(&firmware_info.version_str[version_length],
						 extra_version->string, length);
			version_length += length;
		} else if (status != EFI_NOT_FOUND) {
			return status;
		}

		firmware_info.version_str[version_length] = '\0';

		status = cdk2_coreboot_append_guid_hob(hob, &m_cdk2_firmware_info_hob_guid,
						       &firmware_info, sizeof(firmware_info));
		if (EFI_ERROR(status)) {
			return status;
		}
	} else if (status != EFI_NOT_FOUND) {
		return status;
	}

	status = cdk2_coreboot_find_record(&m_coreboot_handoff, CB_TAG_TPM_PPI_HANDOFF,
					   CDK2_COREBOOT_TPM_PPI_MIN_SIZE, &record);
	if (!EFI_ERROR(status)) {
		tpm_ppi = (const struct cb_tpm_physical_presence *)record;
		tpm_ppi_info = (TCG_PHYSICAL_PRESENCE_INFO){0};
		tpm_ppi_info.ppi_address = tpm_ppi->ppi_address;
		if (tpm_ppi->tpm_version == LB_TPM_VERSION_TPM_VERSION_1_2) {
			tpm_ppi_info.tpm_version = UEFIPAYLOAD_TPM_VERSION_1_2;
		} else if (tpm_ppi->tpm_version == LB_TPM_VERSION_TPM_VERSION_2) {
			tpm_ppi_info.tpm_version = UEFIPAYLOAD_TPM_VERSION_2;
		}

		if ((tpm_ppi->ppi_version >> 4) == 1 && (tpm_ppi->ppi_version & 0x0f) >= 3) {
			tpm_ppi_info.ppi_version = UEFIPAYLOAD_TPM_PPI_VERSION_1_30;
		}

		status = cdk2_coreboot_append_guid_hob(hob,
						       &m_cdk2_tcg_physical_presence_info_hob_guid,
						       &tpm_ppi_info, sizeof(tpm_ppi_info));
		if (EFI_ERROR(status)) {
			return status;
		}
	} else if (status != EFI_NOT_FOUND) {
		return status;
	}
	return EFI_SUCCESS;
}

#if defined(CDK2_COREBOOT_BACKEND_TEST)
EFI_STATUS EFIAPI cdk2_coreboot_test_build_platform_hobs(
	const struct cdk2_coreboot_handoff *coreboot,
	struct cdk2_native_context *context, const struct cdk2_linear_state *state,
	void **handoff)
{
	struct cdk2_coreboot_handoff saved = m_coreboot_handoff;
	EFI_STATUS status;
#if CONFIG_CDK2_LINEAR_BOOT
	struct cdk2_linear_state saved_state = m_linear_state;

	m_linear_state = *state;
#else
	(void)state;
#endif
	m_coreboot_handoff = *coreboot;
	status = cdk2_coreboot_build_platform_hobs(context, handoff);
	m_coreboot_handoff = saved;
#if CONFIG_CDK2_LINEAR_BOOT
	m_linear_state = saved_state;
#endif
	return status;
}
#endif

static EFI_STATUS EFIAPI cdk2_coreboot_find_hob_memory(struct cdk2_native_context *context,
						       UINTN *hob_mem_base)
{
	EFI_STATUS status;
	UINT64 end, limit;
	UINTN index;

	if (context == NULL || hob_mem_base == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	/* entry32.S maps 0..128 GiB while this stage builds HOBs and loads DXE. */
	status = cdk2_coreboot_find_hob_memory_base(&m_coreboot_handoff,
		context->payload_base, context->payload_size, context->hob_region_size,
		CDK2_COREBOOT_TEMP_MAP_LIMIT, hob_mem_base);
	if (EFI_ERROR(status))
		return status;

	/* The fixed minimum only selects a safe RAM range.  Give DXE the entire
	 * authoritative contiguous remainder instead of imposing a 64 MiB pool
	 * ceiling on LoadImage and other boot-services allocations. */
	for (index = 0; index < m_coreboot_handoff.memory_range_count; index++) {
		const struct cdk2_coreboot_memory_range *range =
			&m_coreboot_handoff.memory_ranges[index];

		if (range->type != CB_MEM_RAM || range->size == 0U ||
		    range->base > MAX_UINT64 - range->size)
			continue;
		end = range->base + range->size;
		if (*hob_mem_base < range->base || *hob_mem_base >= end)
			continue;
		limit = end < CDK2_COREBOOT_TEMP_MAP_LIMIT ? end :
			CDK2_COREBOOT_TEMP_MAP_LIMIT;
		limit &= ~(UINT64)(EFI_PAGE_SIZE - 1U);
		if (limit > *hob_mem_base &&
		    limit - *hob_mem_base >= context->hob_region_size &&
		    limit - *hob_mem_base <= MAX_UINTN)
			context->hob_region_size = (UINTN)(limit - *hob_mem_base);
		break;
	}
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
cdk2_coreboot_initialize_floating_point(struct cdk2_native_context *context)
{
#if defined(__x86_64__) && !defined(CDK2_COREBOOT_BACKEND_TEST)
	UINTN cr4;
	UINTN handler;
	UINTN index;
	const UINT32 mxcsr = CDK2_COREBOOT_UEFI_MXCSR;
	struct cdk2_native_idtr idtr;
#endif

	if (context == NULL) {
		return EFI_INVALID_PARAMETER;
	}

#if defined(__x86_64__) && !defined(CDK2_COREBOOT_BACKEND_TEST)
	__asm__ volatile("fninit");
	__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= BIT9;
	__asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
	__asm__ volatile("ldmxcsr %0" : : "m"(mxcsr) : "memory");

	handler = (UINTN)cdk2_native_exception_dead_loop;
	for (index = 0; index < ARRAY_SIZE(m_cdk2_native_idt); index++) {
		m_cdk2_native_idt[index].offset_low = (UINT16)handler;
		m_cdk2_native_idt[index].selector = 0x18;
		m_cdk2_native_idt[index].ist = 0;
		m_cdk2_native_idt[index].attributes = 0x8e;
		m_cdk2_native_idt[index].offset_middle = (UINT16)(handler >> 16);
		m_cdk2_native_idt[index].offset_high = (UINT32)(handler >> 32);
		m_cdk2_native_idt[index].reserved = 0;
	}

	idtr.limit = sizeof(m_cdk2_native_idt) - 1;
	idtr.base = (UINTN)m_cdk2_native_idt;
	__asm__ volatile("lidt %0" : : "m"(idtr));
#endif
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI
cdk2_coreboot_mask_legacy_interrupts(struct cdk2_native_context *context)
{
	if (context == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	/* coreboot owns HPET/IOAPIC routing; only mask the legacy PIC here. */
	cdk2_coreboot_io_write8(CDK2_COREBOOT_8259_MASK_REGISTER_MASTER, 0xFF);
	cdk2_coreboot_io_write8(CDK2_COREBOOT_8259_MASK_REGISTER_SLAVE, 0xFF);
	cdk2_coreboot_io_write8(CDK2_COREBOOT_8259_COMMAND_REGISTER_SLAVE,
				CDK2_COREBOOT_8259_EOI);
	cdk2_coreboot_io_write8(CDK2_COREBOOT_8259_COMMAND_REGISTER_MASTER,
				CDK2_COREBOOT_8259_EOI);
	return EFI_SUCCESS;
}

static EFI_STATUS
cdk2_coreboot_save_hob_append_state(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
				    struct cdk2_coreboot_hob_append_state *state)
{
	EFI_HOB_GENERIC_HEADER *end;

	if (handoff == NULL || state == NULL || handoff->efi_end_of_hob_list == 0) {
		return EFI_INVALID_PARAMETER;
	}

	if (handoff->efi_end_of_hob_list > (EFI_PHYSICAL_ADDRESS)(MAX_UINTN - sizeof(*end))) {
		return EFI_COMPROMISED_DATA;
	}

	end = (EFI_HOB_GENERIC_HEADER *)(UINTN)handoff->efi_end_of_hob_list;
	if (end->hob_type != EFI_HOB_TYPE_END_OF_HOB_LIST || end->hob_length != sizeof(*end)) {
		return EFI_COMPROMISED_DATA;
	}

	state->end_of_hob_list = handoff->efi_end_of_hob_list;
	state->free_memory_bottom = handoff->efi_free_memory_bottom;
	state->end_marker = *end;
	return EFI_SUCCESS;
}

static void
cdk2_coreboot_restore_hob_append_state(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
				       const struct cdk2_coreboot_hob_append_state *state)
{
	if (handoff == NULL || state == NULL || state->end_of_hob_list == 0) {
		return;
	}

	handoff->efi_end_of_hob_list = state->end_of_hob_list;
	handoff->efi_free_memory_bottom = state->free_memory_bottom;
	*(EFI_HOB_GENERIC_HEADER *)(UINTN)state->end_of_hob_list = state->end_marker;
}

static EFI_STATUS cdk2_coreboot_append_loaded_dxe_core_hobs(
	EFI_HOB_HANDOFF_INFO_TABLE *handoff,
	const struct cdk2_direct_image_handoff *direct,
	const EFI_GUID *module_name, EFI_PHYSICAL_ADDRESS image_base, UINTN image_size,
	EFI_PHYSICAL_ADDRESS entry_point)
{
	struct cdk2_coreboot_hob_append_state append_state;
	EFI_STATUS status;

	status = cdk2_coreboot_save_hob_append_state(handoff, &append_state);
	if (EFI_ERROR(status)) {
		return status;
	}

	if (direct == NULL) {
		status = EFI_NOT_FOUND;
		goto failed;
	}
	status = cdk2_coreboot_append_guid_hob(handoff,
		&cdk2_direct_image_handoff_guid, direct, sizeof(*direct));
	if (EFI_ERROR(status))
		goto failed;

	status = cdk2_coreboot_append_memory_allocation_hob(handoff, image_base, image_size,
							    efi_boot_services_code);
	if (EFI_ERROR(status)) {
		goto failed;
	}

	status = cdk2_coreboot_append_module_hob(handoff, module_name, image_base, image_size,
						 entry_point);
	if (EFI_ERROR(status)) {
		goto failed;
	}

	return EFI_SUCCESS;

failed:
	cdk2_coreboot_restore_hob_append_state(handoff, &append_state);
	return status;
}

static EFI_STATUS cdk2_coreboot_select_linked_dxe_core(UINTN payload_start,
	UINTN payload_end, UINTN table_start, UINTN table_end, UINTN pe_start, UINTN pe_end,
	struct cdk2_direct_image_record *dxe_core)
{
	struct cdk2_direct_image_table_view table;
	EFI_STATUS status;

	if (dxe_core == NULL)
		return EFI_INVALID_PARAMETER;
	*dxe_core = (struct cdk2_direct_image_record){0};
	if (table_start == 0U && table_end == 0U && pe_start == 0U && pe_end == 0U)
		return EFI_NOT_FOUND;
	if (table_start == 0U || table_end <= table_start || pe_start == 0U ||
	    pe_end <= pe_start || payload_end <= payload_start ||
	    table_start < payload_start || table_end > pe_start ||
	    pe_end > payload_end ||
	    (table_start & (CDK2_DIRECT_IMAGE_ALIGNMENT - 1U)) != 0U ||
	    (pe_start & (CDK2_DIRECT_IMAGE_ALIGNMENT - 1U)) != 0U)
		return EFI_COMPROMISED_DATA;
	status = cdk2_direct_image_table_open((const void *)table_start,
		table_end - table_start, (const void *)pe_start, pe_end - pe_start,
		&table);
	if (EFI_ERROR(status))
		return status;
	return cdk2_direct_image_table_dxe_core(&table, dxe_core);
}

struct cdk2_coreboot_dxe_source {
	struct cdk2_direct_image_record linked;
	const void *image;
	const EFI_GUID *guid;
	UINTN image_size;
};

static EFI_STATUS cdk2_coreboot_resolve_dxe_core(UINTN payload_start,
	UINTN payload_end, UINTN table_start, UINTN table_end, UINTN pe_start,
	UINTN pe_end,
	struct cdk2_coreboot_dxe_source *source)
{
	EFI_STATUS status;

	if (source == NULL)
		return EFI_INVALID_PARAMETER;
	*source = (struct cdk2_coreboot_dxe_source){0};
	status = cdk2_coreboot_select_linked_dxe_core(payload_start, payload_end,
		table_start, table_end, pe_start, pe_end, &source->linked);
	if (!EFI_ERROR(status)) {
		source->image = source->linked.source;
		source->image_size = source->linked.source_size;
		source->guid = &source->linked.guid;
		return EFI_SUCCESS;
	}
	return status;
}

static EFI_STATUS EFIAPI cdk2_coreboot_load_dxe_core(struct cdk2_native_context *context,
						     EFI_PHYSICAL_ADDRESS *entry_point,
						     EFI_PHYSICAL_ADDRESS *image_base,
						     UINTN *image_size)
{
	struct cdk2_coreboot_dxe_source source;
	struct cdk2_direct_image_handoff direct_handoff;
	EFI_HOB_HANDOFF_INFO_TABLE *handoff;
	EFI_PHYSICAL_ADDRESS destination;
	EFI_PHYSICAL_ADDRESS saved_allocation_bottom;
	EFI_PHYSICAL_ADDRESS saved_allocation_top;
	EFI_PHYSICAL_ADDRESS saved_free_memory_top;
	UINTN available_pages;
	UINTN pages;
	UINTN loaded_image_size;
	EFI_STATUS status;

	if (context == NULL || entry_point == NULL || image_base == NULL ||
	    image_size == NULL || context->hob_list == NULL) {
		return EFI_NOT_FOUND;
	}

	handoff = (EFI_HOB_HANDOFF_INFO_TABLE *)context->hob_list;
	status = cdk2_coreboot_resolve_dxe_core(
		(UINTN)__cdk2_image_start, (UINTN)__cdk2_image_end,
		(UINTN)__cdk2_image_table_start, (UINTN)__cdk2_image_table_end,
		(UINTN)__cdk2_pe_start, (UINTN)__cdk2_pe_end,
		&source);
	if (EFI_ERROR(status))
		return status;
	direct_handoff = (struct cdk2_direct_image_handoff) {
		.revision = CDK2_DIRECT_IMAGE_HANDOFF_REVISION,
		.size = sizeof(direct_handoff),
		.payload_base = (UINTN)__cdk2_image_start,
		.payload_size = (UINTN)__cdk2_image_end -
			(UINTN)__cdk2_image_start,
		.table_base = (UINTN)__cdk2_image_table_start,
		.table_size = (UINTN)__cdk2_image_table_end -
			(UINTN)__cdk2_image_table_start,
		.pe_base = (UINTN)__cdk2_pe_start,
		.pe_size = (UINTN)__cdk2_pe_end - (UINTN)__cdk2_pe_start,
	};

	if (context->allocation_top < context->allocation_bottom) {
		return EFI_COMPROMISED_DATA;
	}

	available_pages = (context->allocation_top - context->allocation_bottom) / EFI_PAGE_SIZE;
	pages = (available_pages < CDK2_COREBOOT_DXE_MAX_PAGES) ? available_pages :
								 CDK2_COREBOOT_DXE_MAX_PAGES;
	if (pages == 0) {
		return EFI_OUT_OF_RESOURCES;
	}

	saved_allocation_bottom = context->allocation_bottom;
	saved_allocation_top = context->allocation_top;
	saved_free_memory_top = handoff->efi_free_memory_top;
	status = cdk2_native_allocate_pages(context, pages, &destination);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = cdk2_native_load_pe32_plus(source.image, source.image_size, destination,
					    pages * EFI_PAGE_SIZE, image_base, image_size,
					    entry_point);
	if (EFI_ERROR(status)) {
		goto failed;
	}

	loaded_image_size = EFI_SIZE_TO_PAGES(*image_size) * EFI_PAGE_SIZE;
	status = cdk2_coreboot_append_loaded_dxe_core_hobs(
		handoff, &direct_handoff,
		source.guid, *image_base, loaded_image_size, *entry_point);
	if (EFI_ERROR(status)) {
		goto failed;
	}

	return EFI_SUCCESS;

failed:
	context->allocation_bottom = saved_allocation_bottom;
	context->allocation_top = saved_allocation_top;
	handoff->efi_free_memory_top = saved_free_memory_top;
	*entry_point = 0;
	*image_base = 0;
	*image_size = 0;
	return status;
}

static void CDK2_COREBOOT_NORETURN cdk2_coreboot_jump_to_dxe_core(
	EFI_PHYSICAL_ADDRESS entry_point, void *hob_list, void *stack_top)
{
#if defined(__x86_64__)
	__asm__ volatile(
		"cli\n\t"
		"mov %[stack], %%rsp\n\t"
		"lea 1f(%%rip), %%r10\n\t"
		"mov %%r10, (%%rsp)\n\t"
		"xor %%rbp, %%rbp\n\t"
		"mov %[hob], %%rcx\n\t"
		"xor %%rdx, %%rdx\n\t"
		"xor %%r8, %%r8\n\t"
		"xor %%r9, %%r9\n\t"
		"jmp *%[entry]\n\t"
		"1:\n\t"
		"cli\n\t"
		"2:\n\t"
		"hlt\n\t"
		"jmp 2b\n\t"
		:
		: [stack] "r"(stack_top), [hob] "c"(hob_list),
		  [entry] "a"((UINTN)entry_point)
		: "rdx", "r8", "r9", "r10", "memory");
#endif
	__builtin_unreachable();
}

static void *cdk2_coreboot_dxe_stack_top(EFI_PHYSICAL_ADDRESS stack_base,
	UINTN stack_pages)
{
	return (void *)(UINTN)(stack_base + stack_pages * EFI_PAGE_SIZE - 0x28U);
}

static EFI_STATUS EFIAPI cdk2_coreboot_transfer(struct cdk2_native_context *context)
{
	EFI_HOB_HANDOFF_INFO_TABLE *handoff;
	EFI_PHYSICAL_ADDRESS saved_allocation_bottom;
	EFI_PHYSICAL_ADDRESS saved_allocation_top;
	EFI_PHYSICAL_ADDRESS saved_free_memory_top;
	EFI_PHYSICAL_ADDRESS stack_base;
	UINTN stack_pages;
	EFI_STATUS status;

	if (context == NULL || context->hob_list == NULL || context->image_entry_point == 0) {
		return EFI_INVALID_PARAMETER;
	}

	status = cdk2_native_handoff(context);
	if (EFI_ERROR(status)) {
		return status;
	}

	handoff = (EFI_HOB_HANDOFF_INFO_TABLE *)context->hob_list;
	saved_allocation_bottom = context->allocation_bottom;
	saved_allocation_top = context->allocation_top;
	saved_free_memory_top = handoff->efi_free_memory_top;
	stack_pages = 0x20;
	status = cdk2_native_allocate_pages(context, stack_pages, &stack_base);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = cdk2_coreboot_append_stack_hob((EFI_HOB_HANDOFF_INFO_TABLE *)context->hob_list,
						stack_base, stack_pages * EFI_PAGE_SIZE);
	if (EFI_ERROR(status)) {
		context->allocation_bottom = saved_allocation_bottom;
		context->allocation_top = saved_allocation_top;
		handoff->efi_free_memory_top = saved_free_memory_top;
		return status;
	}

	cdk2_coreboot_jump_to_dxe_core(
		context->image_entry_point, context->hob_list,
		cdk2_coreboot_dxe_stack_top(stack_base, stack_pages));
	return EFI_DEVICE_ERROR;
}

#if defined(CDK2_COREBOOT_BACKEND_TEST)
EFI_STATUS EFIAPI cdk2_coreboot_test_select_linked_dxe_core(
	UINTN payload_start, UINTN payload_end, UINTN table_start,
	UINTN table_end, UINTN pe_start, UINTN pe_end,
	struct cdk2_direct_image_record *dxe_core)
{
	return cdk2_coreboot_select_linked_dxe_core(payload_start, payload_end,
		table_start, table_end, pe_start, pe_end, dxe_core);
}

EFI_STATUS EFIAPI cdk2_coreboot_test_resolve_dxe_core(
	UINTN payload_start, UINTN payload_end, UINTN table_start,
	UINTN table_end, UINTN pe_start, UINTN pe_end, BOOLEAN *direct)
{
	struct cdk2_coreboot_dxe_source source;
	EFI_STATUS status;

	if (direct == NULL)
		return EFI_INVALID_PARAMETER;
	*direct = FALSE;
	status = cdk2_coreboot_resolve_dxe_core(payload_start, payload_end,
		table_start, table_end, pe_start, pe_end, &source);
	if (!EFI_ERROR(status))
		*direct = TRUE;
	return status;
}

EFI_STATUS cdk2_coreboot_test_mask_legacy_interrupts(
	struct cdk2_native_context *context)
{
	return cdk2_coreboot_mask_legacy_interrupts(context);
}

void *cdk2_coreboot_test_dxe_stack_top(EFI_PHYSICAL_ADDRESS stack_base,
	UINTN stack_pages)
{
	return cdk2_coreboot_dxe_stack_top(stack_base, stack_pages);
}

EFI_STATUS EFIAPI cdk2_coreboot_test_append_serial_hobs(
	EFI_HOB_HANDOFF_INFO_TABLE *hob,
	const struct cdk2_coreboot_handoff *coreboot)
{
	return cdk2_coreboot_append_serial_hobs(hob, coreboot);
}

EFI_STATUS EFIAPI cdk2_coreboot_test_append_tsc_info_hob(
	EFI_HOB_HANDOFF_INFO_TABLE *hob,
	const struct cdk2_coreboot_handoff *coreboot)
{
	return cdk2_coreboot_append_tsc_info_hob(hob, coreboot);
}

#if CONFIG_CDK2_NATIVE_LOCAL_APIC_TIMER
EFI_STATUS EFIAPI cdk2_coreboot_test_append_local_apic_timer_info_hob(
	EFI_HOB_HANDOFF_INFO_TABLE *hob,
	const struct cdk2_coreboot_handoff *coreboot)
{
	return cdk2_coreboot_append_local_apic_timer_info_hob(hob, coreboot);
}
#endif

EFI_STATUS EFIAPI cdk2_coreboot_test_append_acpi_hobs(
	EFI_HOB_HANDOFF_INFO_TABLE *hob,
	const struct cdk2_coreboot_handoff *coreboot)
{
	return cdk2_coreboot_append_acpi_hobs(hob, coreboot);
}

EFI_STATUS EFIAPI cdk2_coreboot_test_append_diagnostic_hob(
	EFI_HOB_HANDOFF_INFO_TABLE hob[],
	const struct cdk2_coreboot_handoff *coreboot)
{
	struct cdk2_diag_handoff diagnostic;
	EFI_STATUS status;

	status = cdk2_coreboot_fill_diagnostic_handoff(coreboot, &diagnostic);
	if (EFI_ERROR(status))
		return status;
	return cdk2_coreboot_append_guid_hob(hob, &cdk2_diag_handoff_guid,
		&diagnostic, sizeof(diagnostic));
}

EFI_STATUS EFIAPI
cdk2_coreboot_test_append_capsule_hobs(EFI_HOB_HANDOFF_INFO_TABLE hob[],
				       const struct cdk2_coreboot_handoff *coreboot)
{
	return cdk2_coreboot_append_capsule_hobs(hob, coreboot);
}

struct cdk2_coreboot_test_cbmem_result
EFIAPI
cdk2_coreboot_test_find_cbmem_entry(const struct cdk2_coreboot_handoff *coreboot, UINT32 id,
				    UINT32 minimum_size)
{
	struct cdk2_coreboot_test_cbmem_result result;

	result = (struct cdk2_coreboot_test_cbmem_result){0};
	result.status = cdk2_coreboot_find_cbmem_entry(coreboot, id, minimum_size, &result.base,
						      &result.size);
	return result;
}

struct cdk2_coreboot_test_cbmem_result
EFIAPI
cdk2_coreboot_test_find_cbmem_console(const struct cdk2_coreboot_handoff *coreboot)
{
	struct cdk2_coreboot_test_cbmem_result result;

	result = (struct cdk2_coreboot_test_cbmem_result){0};
	result.status = cdk2_coreboot_find_cbmem_console(coreboot, &result.base,
		&result.size);
	return result;
}

struct cdk2_coreboot_test_cbmem_result
EFIAPI
cdk2_coreboot_test_find_acpi_rsdp(const struct cdk2_coreboot_handoff *coreboot)
{
	struct cdk2_coreboot_test_cbmem_result result;

	result = (struct cdk2_coreboot_test_cbmem_result){0};
	result.status = cdk2_coreboot_find_acpi_rsdp(coreboot, &result.base);
	return result;
}

EFI_STATUS
EFIAPI
cdk2_coreboot_test_build_acpi_board_info(EFI_PHYSICAL_ADDRESS rsdp_address, void *board_info)
{
	return cdk2_coreboot_build_acpi_board_info(
		rsdp_address, (ACPI_BOARD_INFO *)board_info, NULL, NULL, NULL, NULL);
}

BOOLEAN
EFIAPI
cdk2_coreboot_test_acpi_s5_sleep_type(const EFI_ACPI_DESCRIPTION_HEADER *dsdt,
	UINT8 *sleep_type)
{
	return cdk2_coreboot_acpi_s5_sleep_type(dsdt, sleep_type);
}

EFI_STATUS
EFIAPI
cdk2_coreboot_test_validate_framebuffer(const struct cb_framebuffer *framebuffer)
{
	return cdk2_coreboot_validate_framebuffer_geometry(framebuffer);
}

EFI_STATUS
EFIAPI
cdk2_coreboot_test_append_tpm_event_hobs(void *handoff,
					 EFI_ACPI_DESCRIPTION_HEADER *tpm2_table)
{
	return cdk2_coreboot_append_tpm_event_hobs(
		(EFI_HOB_HANDOFF_INFO_TABLE *)handoff, tpm2_table);
}

EFI_STATUS
EFIAPI
cdk2_coreboot_test_append_smbios_hob(void *handoff,
				     const struct cdk2_coreboot_handoff *coreboot)
{
	return cdk2_coreboot_append_smbios_hob((EFI_HOB_HANDOFF_INFO_TABLE *)handoff,
					       coreboot);
}

EFI_STATUS
EFIAPI
cdk2_coreboot_test_append_smmstore_hobs(void *handoff,
	const struct cdk2_coreboot_handoff *coreboot)
{
	return cdk2_coreboot_append_smmstore_hobs(
		(EFI_HOB_HANDOFF_INFO_TABLE *)handoff, coreboot);
}

EFI_STATUS
EFIAPI
cdk2_coreboot_test_append_smram_hob(void *handoff,
	const struct cdk2_coreboot_handoff *coreboot)
{
	return cdk2_coreboot_append_smram_hob(
		(EFI_HOB_HANDOFF_INFO_TABLE *)handoff, coreboot);
}

EFI_STATUS
EFIAPI
cdk2_coreboot_test_resolve_deadline_tsc_info(
	const struct cdk2_coreboot_handoff *coreboot,
	struct cdk2_deadline_tsc_info *info)
{
	return cdk2_coreboot_resolve_deadline_tsc_info(coreboot, info);
}

EFI_STATUS
EFIAPI
cdk2_coreboot_test_transfer(struct cdk2_native_context *context)
{
	return cdk2_coreboot_transfer(context);
}

EFI_STATUS
EFIAPI
cdk2_coreboot_test_append_loaded_dxe_core_hobs(void *handoff,
					       BOOLEAN direct,
					       const void *module_name,
					       EFI_PHYSICAL_ADDRESS image_base,
					       UINTN image_size,
					       EFI_PHYSICAL_ADDRESS entry_point)
{
	struct cdk2_direct_image_handoff direct_handoff = {
		.revision = CDK2_DIRECT_IMAGE_HANDOFF_REVISION,
		.size = sizeof(direct_handoff),
	};

	return cdk2_coreboot_append_loaded_dxe_core_hobs(
		(EFI_HOB_HANDOFF_INFO_TABLE *)handoff,
		direct ? &direct_handoff : NULL, (const EFI_GUID *)module_name,
		image_base, image_size,
		entry_point);
}
#endif

#if CONFIG_CDK2_CBMEM_TIMESTAMPS || defined(CDK2_COREBOOT_BACKEND_TEST)
static UINT64 cdk2_coreboot_apply_tsc_frequency(
	struct cdk2_diag_timestamp_table *timestamps, UINT32 frequency_khz)
{
	UINT32 frequency_mhz;

	if (timestamps->tick_freq_mhz != 0U)
		return (UINT64)timestamps->tick_freq_mhz * 1000000U;
	if (frequency_khz < 1000U)
		return 0U;
	frequency_mhz = frequency_khz / 1000U;
	if (frequency_mhz > MAX_UINT16)
		return 0U;
	timestamps->tick_freq_mhz = frequency_mhz;
	return (UINT64)frequency_mhz * 1000000U;
}

#ifdef CDK2_COREBOOT_BACKEND_TEST
UINT64 EFIAPI cdk2_coreboot_test_apply_tsc_frequency(
	struct cdk2_diag_timestamp_table *timestamps, UINT32 frequency_khz)
{
	return cdk2_coreboot_apply_tsc_frequency(timestamps, frequency_khz);
}
#endif
#endif

static EFI_STATUS cdk2_coreboot_validate_handoff(UINTN bootloader_parameter)
{
	struct cdk2_coreboot_handoff handoff;
	struct cdk2_diag_config diagnostic = {0};
#if CONFIG_CDK2_SERIAL
	const void *serial_record;
#endif
#if CONFIG_CDK2_CBMEM_TIMESTAMPS
	const void *record;
#endif
#if CONFIG_CDK2_SPI_CONSOLE
	struct cdk2_diag_handoff diagnostic_handoff;
#endif
#if CONFIG_CDK2_CBMEM_TIMESTAMPS
	EFI_PHYSICAL_ADDRESS timestamp_base;
	UINT32 timestamp_size;
	const struct cb_tsc_info *tsc_info;
#endif
	EFI_STATUS status;

	status = cdk2_coreboot_parse(bootloader_parameter, &handoff);
	if (EFI_ERROR(status))
		return status;
#if CONFIG_CDK2_SERIAL
	status = cdk2_coreboot_find_unique_record(&handoff, CB_TAG_SERIAL,
		CDK2_COREBOOT_SERIAL_MIN_SIZE, &serial_record);
	if (EFI_ERROR(status) && status != EFI_NOT_FOUND)
		return status;
#endif
#if CONFIG_CDK2_CBMEM_TIMESTAMPS
	status = cdk2_coreboot_find_unique_record(&handoff, CB_TAG_TSC_INFO,
		CDK2_COREBOOT_TSC_INFO_MIN_SIZE, &record);
	if (EFI_ERROR(status) && status != EFI_NOT_FOUND)
		return status;
#endif
#if CONFIG_PAYLOAD_DMA_HANDOFF
	status = cdk2_dma_handoff_import(&handoff, &m_dma_handoff);
	if (EFI_ERROR(status))
		return status;
	handoff.dma_handoff = &m_dma_handoff;
#endif
	m_coreboot_handoff = handoff;
#if CONFIG_CDK2_SERIAL
	if (serial_record != NULL) {
		const struct cb_serial *serial = serial_record;

		if (serial->type == CB_SERIAL_TYPE_IO_MAPPED &&
		    cdk2_uart_registers_valid(CDK2_DIAG_SERIAL_PIO, serial->baseaddr,
			serial->regwidth, serial->regwidth, 5U)) {
			diagnostic.putc = cdk2_diagnostic_io_putc;
			m_diagnostic_mmio_uart.base = serial->baseaddr;
			m_diagnostic_mmio_uart.stride = serial->regwidth;
			diagnostic.putc_context = &m_diagnostic_mmio_uart;
			diagnostic.putc_provenance = CDK2_DIAG_SINK_SERIAL;
		} else if (serial->type == CB_SERIAL_TYPE_MEMORY_MAPPED &&
			   cdk2_uart_registers_valid(CDK2_DIAG_SERIAL_MMIO, serial->baseaddr,
				serial->regwidth, serial->regwidth, 5U)) {
			m_diagnostic_mmio_uart.base = serial->baseaddr;
			m_diagnostic_mmio_uart.stride = serial->regwidth;
			m_diagnostic_mmio_uart.width = serial->regwidth;
			diagnostic.putc = cdk2_diagnostic_mmio_putc;
			diagnostic.putc_context = &m_diagnostic_mmio_uart;
			diagnostic.putc_provenance = CDK2_DIAG_SINK_SERIAL;
		}
	}
#endif
	/* CBMEM remains coreboot-owned and valid across the payload handoff. */
#if CONFIG_CDK2_CBMEM_TIMESTAMPS
	if (!EFI_ERROR(cdk2_coreboot_find_cbmem_entry(&handoff,
	    CBMEM_ID_TIMESTAMP, sizeof(struct cdk2_diag_timestamp_table),
	    &timestamp_base, &timestamp_size))) {
		diagnostic.timestamps = (void *)(UINTN)timestamp_base;
		diagnostic.timestamp_bytes = timestamp_size;
		diagnostic.clock = cdk2_diagnostic_clock;
		if (diagnostic.timestamps->tick_freq_mhz == 0U && record != NULL) {
			tsc_info = record;
			diagnostic.clock_hz = cdk2_coreboot_apply_tsc_frequency(
				diagnostic.timestamps, tsc_info->freq_khz);
		}
		if (diagnostic.clock_hz == 0U)
			diagnostic.clock_hz = cdk2_coreboot_apply_tsc_frequency(
				diagnostic.timestamps, 0U);
	}
#endif
#if CONFIG_CDK2_CBMEM_CONSOLE
	{
		EFI_PHYSICAL_ADDRESS console_base;
		UINT32 console_size;

		if (!EFI_ERROR(cdk2_coreboot_find_cbmem_console(&handoff,
		    &console_base, &console_size))) {
			diagnostic.cbmem_console = (void *)(UINTN)console_base;
			diagnostic.cbmem_console_bytes = console_size;
		}
	}
#endif
#if CONFIG_CDK2_SPI_CONSOLE
	status = cdk2_coreboot_fill_diagnostic_handoff(&handoff, &diagnostic_handoff);
	if (EFI_ERROR(status))
		return status;
	if (diagnostic_handoff.spi_console_buffer != 0U) {
		m_diagnostic_spi_console_remaining =
			diagnostic_handoff.spi_console_boot_limit;
		m_diagnostic_spi_console_active = TRUE;
		diagnostic.spi_console_buffer =
			(void *)(UINTN)diagnostic_handoff.spi_console_buffer;
		diagnostic.spi_console_buffer_bytes =
			diagnostic_handoff.spi_console_buffer_bytes;
		diagnostic.spi_console_max_chunk =
			diagnostic_handoff.spi_console_max_chunk;
		diagnostic.spi_console_boot_limit =
			diagnostic_handoff.spi_console_boot_limit;
		diagnostic.spi_console_apm_cmd = diagnostic_handoff.spi_console_apm_cmd;
		diagnostic.spi_console_remaining = &m_diagnostic_spi_console_remaining;
	}
#endif
	cdk2_diag_configure(&diagnostic);
	return EFI_SUCCESS;
}

#if defined(CDK2_COREBOOT_BACKEND_TEST)
EFI_STATUS EFIAPI cdk2_coreboot_test_validate_handoff(UINTN bootloader_parameter)
{
	return cdk2_coreboot_validate_handoff(bootloader_parameter);
}
#endif

#if CONFIG_CDK2_LINEAR_BOOT
struct cdk2_coreboot_linear_context {
	UINTN bootloader_parameter;
	uint64_t deferred_handoff_start;
};

static uint64_t cdk2_coreboot_linear_clock(void *context)
{
	(void)context;
	return cdk2_diagnostic_clock(NULL);
}

static uint64_t cdk2_coreboot_linear_validate(void *context)
{
	const struct cdk2_coreboot_linear_context *linear = context;

	return cdk2_coreboot_validate_handoff(linear->bootloader_parameter);
}

static EFI_STATUS cdk2_coreboot_splash_record(
	const struct cdk2_coreboot_handoff *handoff,
	const struct cb_framebuffer *framebuffer,
	const struct cb_boot_splash **splash)
{
	const struct cb_boot_splash *candidate;
	const void *record;
	UINT64 framebuffer_address;
	UINT64 bmp_address;
	UINT64 bmp_allocation_size;
	EFI_PHYSICAL_ADDRESS cbmem_address;
	const UINT8 *bmp;
	UINT64 bmp_row_bits, bmp_row_size, bmp_pixel_size;
	UINT32 bmp_width, bmp_height;
	UINT32 bmp_offset;
	UINT32 cbmem_size;
	UINT16 bmp_bits_per_pixel;
	EFI_STATUS status;

	if (handoff == NULL || framebuffer == NULL || splash == NULL)
		return EFI_INVALID_PARAMETER;
	*splash = NULL;
	status = cdk2_coreboot_find_unique_record(handoff,
		CB_TAG_BOOT_SPLASH, sizeof(*candidate), &record);
	if (EFI_ERROR(status))
		return status;
	candidate = record;
	framebuffer_address = cdk2_coreboot_read64(
		&candidate->framebuffer_address);
	bmp_address = cdk2_coreboot_read64(&candidate->bmp_address);
	if (candidate->size != sizeof(*candidate) ||
	    candidate->bmp_size < 54U ||
	    candidate->revision != CB_BOOT_SPLASH_REVISION ||
	    candidate->flags != (CB_BOOT_SPLASH_DISPLAYED | CB_BOOT_SPLASH_BMP) ||
	    framebuffer_address != cdk2_coreboot_framebuffer_address(framebuffer) ||
	    candidate->image_width == 0U || candidate->image_height == 0U ||
	    candidate->image_offset_x > framebuffer->x_resolution ||
	    candidate->image_width >
		framebuffer->x_resolution - candidate->image_offset_x ||
	    candidate->image_offset_y > framebuffer->y_resolution ||
	    candidate->image_height >
		framebuffer->y_resolution - candidate->image_offset_y)
		return EFI_COMPROMISED_DATA;
	status = cdk2_coreboot_find_cbmem_entry(handoff, CBMEM_ID_BOOT_SPLASH,
		54U, &cbmem_address, &cbmem_size);
	if (EFI_ERROR(status))
		return status;
	bmp_allocation_size = ALIGN_VALUE((UINT64)candidate->bmp_size, EFI_PAGE_SIZE);
	if ((bmp_address & EFI_PAGE_MASK) != 0U ||
	    bmp_address < cbmem_address ||
	    bmp_allocation_size > cbmem_size ||
	    bmp_address - cbmem_address > cbmem_size - bmp_allocation_size)
		return EFI_COMPROMISED_DATA;
	bmp = (const UINT8 *)(UINTN)bmp_address;
	bmp_width = cdk2_coreboot_read32(bmp + 18U);
	bmp_height = cdk2_coreboot_read32(bmp + 22U);
	bmp_offset = cdk2_coreboot_read32(bmp + 10U);
	bmp_bits_per_pixel = cdk2_coreboot_read16(bmp + 28U);
	bmp_row_bits = (UINT64)bmp_width * bmp_bits_per_pixel;
	bmp_row_size = ((bmp_row_bits + 31U) / 32U) * 4U;
	bmp_pixel_size = bmp_row_size != 0U &&
		bmp_height > MAX_UINT64 / bmp_row_size ? MAX_UINT64 :
		bmp_row_size * bmp_height;
	if (bmp[0] != 'B' || bmp[1] != 'M' ||
		cdk2_coreboot_read32(bmp + 2U) != candidate->bmp_size ||
		bmp_offset < 54U || bmp_offset >= candidate->bmp_size ||
		cdk2_coreboot_read32(bmp + 14U) != 40U ||
		cdk2_coreboot_read16(bmp + 26U) != 1U ||
		(bmp_bits_per_pixel != 1U && bmp_bits_per_pixel != 4U &&
		 bmp_bits_per_pixel != 8U && bmp_bits_per_pixel != 24U &&
		 bmp_bits_per_pixel != 32U) ||
		cdk2_coreboot_read32(bmp + 30U) != 0U ||
		bmp_row_bits > MAX_UINT32 ||
		bmp_pixel_size > candidate->bmp_size - bmp_offset ||
		!((bmp_width == candidate->image_width &&
		   bmp_height == candidate->image_height) ||
		  (bmp_width == candidate->image_height &&
		   bmp_height == candidate->image_width)))
		return EFI_COMPROMISED_DATA;
	*splash = candidate;
	return EFI_SUCCESS;
}

#if defined(CDK2_COREBOOT_BACKEND_TEST)
EFI_STATUS EFIAPI cdk2_coreboot_test_splash_record(
	const struct cdk2_coreboot_handoff *handoff,
	const struct cb_framebuffer *framebuffer,
	const struct cb_boot_splash **splash)
{
	return cdk2_coreboot_splash_record(handoff, framebuffer, splash);
}
#endif

static uint64_t cdk2_coreboot_linear_splash(void *context)
{
	const struct cb_boot_splash *splash;
	const struct cb_framebuffer *framebuffer;
	const void *record;
	EFI_STATUS status;

	(void)context;
	status = cdk2_coreboot_find_unique_record(&m_coreboot_handoff,
		CB_TAG_FRAMEBUFFER, CDK2_COREBOOT_FRAMEBUFFER_MIN_SIZE, &record);
	if (EFI_ERROR(status))
		return status;
	framebuffer = record;
	status = cdk2_coreboot_validate_framebuffer_geometry(framebuffer);
	if (!EFI_ERROR(status))
		status = cdk2_coreboot_authorize_framebuffer(
			&m_coreboot_handoff, framebuffer);
	if (!EFI_ERROR(status)) {
		EFI_STATUS splash_status = cdk2_coreboot_splash_record(
			&m_coreboot_handoff, framebuffer, &splash);

		if (EFI_ERROR(splash_status))
			status = cdk2_early_splash_draw(framebuffer);
	}
	if (!EFI_ERROR(status))
		status = cdk2_linear_capture_splash(&m_linear_state,
			cdk2_coreboot_framebuffer_address(framebuffer), framebuffer->bytes_per_line,
			framebuffer->x_resolution, framebuffer->y_resolution);
	if (!EFI_ERROR(status) && splash != NULL)
		status = cdk2_linear_capture_coreboot_splash(&m_linear_state,
			splash->image_offset_x, splash->image_offset_y,
			splash->image_width, splash->image_height,
			cdk2_coreboot_read64(&splash->bmp_address), splash->bmp_size);
	return status;
}

#if defined(CDK2_COREBOOT_BACKEND_TEST)
EFI_STATUS EFIAPI cdk2_coreboot_test_linear_splash(
	const struct cdk2_coreboot_handoff *coreboot)
{
	struct cdk2_coreboot_handoff saved = m_coreboot_handoff;
	struct cdk2_linear_state saved_state = m_linear_state;
	EFI_STATUS status;

	m_coreboot_handoff = *coreboot;
	cdk2_linear_state_init(&m_linear_state);
	status = cdk2_coreboot_linear_splash(NULL);
	m_coreboot_handoff = saved;
	m_linear_state = saved_state;
	return status;
}
#endif

static void cdk2_coreboot_linear_report(enum cdk2_linear_phase_id phase,
	const char *name, uint64_t status, uint64_t elapsed, uint8_t required,
	uint8_t begin, void *context)
{
	struct cdk2_coreboot_linear_context *linear = context;

	(void)required;
	if (phase == CDK2_LINEAR_HANDOFF_VALIDATE && begin) {
		/* The coreboot table must be validated before its diagnostic sink can
		 * be trusted. Capture the boundary now and publish it retroactively
		 * once HANDOFF_VALIDATE has configured that sink. */
		linear->deferred_handoff_start = cdk2_diagnostic_clock(NULL);
		return;
	}
	if (phase == CDK2_LINEAR_HANDOFF_VALIDATE) {
		/* A failed validation has not established a trustworthy sink. */
		if (EFI_ERROR(status))
			return;
		cdk2_diag_phase_event_at(CDK2_EVENT_LINEAR_PHASE_BEGIN(phase),
			linear->deferred_handoff_start, name, "start",
			EFI_NOT_STARTED, 0U);
	}
	cdk2_diag_phase_event(begin ? CDK2_EVENT_LINEAR_PHASE_BEGIN(phase) :
		CDK2_EVENT_LINEAR_PHASE_END(phase), name,
		begin ? "start" : "complete", status, elapsed);
}

static EFI_STATUS cdk2_coreboot_run_native_prefix(UINTN bootloader_parameter)
{
	static const struct cdk2_linear_phase phases[] = {
		{ CDK2_LINEAR_HANDOFF_VALIDATE, "HANDOFF_VALIDATE",
			cdk2_coreboot_linear_validate, TRUE },
		{ CDK2_LINEAR_SPLASH_EARLY, "SPLASH_EARLY",
			cdk2_coreboot_linear_splash, FALSE },
	};
	struct cdk2_coreboot_linear_context context = {
		.bootloader_parameter = bootloader_parameter,
	};
	struct cdk2_linear_plan plan = {
		.phases = phases,
		.count = ARRAY_SIZE(phases),
		.clock = cdk2_coreboot_linear_clock,
		.report = cdk2_coreboot_linear_report,
		.context = &context,
	};
	size_t completed;

	cdk2_linear_state_init(&m_linear_state);
	return cdk2_linear_run(&plan, &m_linear_state, &completed);
}
#endif

EFI_STATUS
EFIAPI
cdk2_platform_initialize_native_context(struct cdk2_native_context *context,
					UINTN bootloader_parameter)
{
	EFI_STATUS status;
	UINTN image_start;
	UINTN image_end;

	image_start = (UINTN)__cdk2_image_start;
	image_end = (UINTN)__cdk2_image_end;
	if (context == NULL || image_end <= image_start) {
		return EFI_INVALID_PARAMETER;
	}

	status = cdk2_native_initialize_stage_context(context, bootloader_parameter);
	if (EFI_ERROR(status)) {
		return status;
	}

	cdk2_diag_configure(NULL);
#if CONFIG_CDK2_LINEAR_BOOT
	status = cdk2_coreboot_run_native_prefix(bootloader_parameter);
#else
	status = cdk2_coreboot_validate_handoff(bootloader_parameter);
#endif
	if (EFI_ERROR(status))
		return status;
	context->payload_base = (EFI_PHYSICAL_ADDRESS)image_start;
	context->payload_size = image_end - image_start;
	context->hob_region_size = CDK2_COREBOOT_HOB_REGION_SIZE;
	context->ops.build_platform_hobs = cdk2_coreboot_build_platform_hobs;
	context->ops.find_hob_memory = cdk2_coreboot_find_hob_memory;
	context->ops.initialize_floating_point = cdk2_coreboot_initialize_floating_point;
	context->ops.mask_legacy_interrupts = cdk2_coreboot_mask_legacy_interrupts;
	context->ops.load_dxe_core = cdk2_coreboot_load_dxe_core;
	context->ops.transfer = cdk2_coreboot_transfer;
	return EFI_SUCCESS;
}
