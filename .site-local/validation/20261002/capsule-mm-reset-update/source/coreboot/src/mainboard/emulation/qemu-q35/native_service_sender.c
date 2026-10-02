/* SPDX-License-Identifier: GPL-2.0-only */

#include "native_service.h"
#include <acpi/acpi.h>
#include <arch/cpu.h>
#include <boot/capsule_delivery_policy.h>
#include <boot/coreboot_tables.h>
#include <boot/payload_mm_authvar_presence_publication.h>
#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot_device.h>
#include <cbfs.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_tuple_trigger.h>
#include <device/fw_cfg.h>
#include <fmap.h>
#include <halt.h>
#include <security/memory/memory.h>
#include <string.h>

#if !ENV_RAMSTAGE
#error "Q35 native service sender is ramstage-only"
#endif

static struct {
	bool complete;
	const struct lb_header *header;
	struct lb_authvar_service_endpoint *record;
	struct q35_native_bootstrap_frame response;
} publication;

static uint64_t trigger(uint32_t request, uint32_t address)
{
	enum cbfs_type type = CBFS_TYPE_RAW;
	size_t size;
	const uint8_t *leaf = cbfs_ro_type_map("q35/native-apmc-bootstrap", &size, &type);
	const struct region_device *root = boot_device_ro();
	uint32_t eax = request, ecx = address;
	const uint16_t dx = 0xb2;

	if (!root || !leaf || type != CBFS_TYPE_RAW || size != 2 ||
	    leaf[0] != 0xee || leaf[1] != 0xc3 || !region_device_sz(root) ||
	    region_device_sz(root) > UINT32_MAX ||
	    (uintptr_t)leaf < 0x100000000ULL - region_device_sz(root) ||
	    (uintptr_t)leaf > UINT32_MAX - 1U)
		die("Q35 native service: invalid bootstrap flash leaf\n");
	__asm__ __volatile__("call *%3" : "+a" (eax), "+c" (ecx)
		: "d" (dx), "r" (leaf) : "memory", "cc");
	return ((uint64_t)ecx << 32) | eax;
}

uint64_t smm_invocation_tuple_trigger(void)
{
	/* No presence caller is installed by the service-only composition. */
	return trigger(UINT32_MAX, UINT32_MAX);
}

bool platform_payload_mm_authvar_presence_required(void)
{
	/* Early opt-in reserves backing; it is not service or presence admission. */
	return fw_cfg_max_cpus() == 1 && cpuid_get_max_func() >= 1 &&
		(cpuid_ecx(1) & (1U << 30));
}

static bool receipt_current(const struct bootmem_reservation_receipt *receipt,
	enum bootmem_type tag, uint64_t size)
{
	struct bootmem_aligned_reservation reservation;

	return receipt->tag == tag && receipt->bytes == size &&
		!bootmem_aligned_reservation_query(&receipt->handle, &reservation) &&
		!reservation.reserved && reservation.tag == tag &&
		reservation.base == receipt->base && reservation.size == size;
}

void q35_publish_native_service_table(struct lb_header *header)
{
	struct payload_mm_authvar_presence_tuple_sender sender;
	struct bootmem_reservation_receipt boot_private;
	struct payload_mm_authvar_presence_bootstrap_receipts receipts;
	struct q35_native_bootstrap_frame *frame;
	struct region_device probe, store, core;
	const struct region_device *root = boot_device_ro();
	volatile uint8_t *byte;
	uint64_t wire, media_size;
	bool required;

	if (publication.complete || !header ||
	    payload_mm_authvar_presence_publication_loader_required(&required) != CB_SUCCESS ||
	    !required || !mainboard_authvar_presence_cold_boot() || !root ||
	    !(media_size = region_device_sz(root)) || media_size < 4096 ||
	    media_size > UINT32_MAX ||
	    fmap_locate_area_as_rdev("SECURE_PROBE", &probe) ||
	    fmap_locate_area_as_rdev("SMMSTORE", &store) ||
	    fmap_locate_area_as_rdev("COREBOOT", &core) ||
	    region_device_offset(&store) > media_size ||
	    region_device_sz(&store) > media_size - region_device_offset(&store) ||
	    region_device_sz(&probe) != 4096 ||
	    region_device_offset(&probe) > media_size - 4096 ||
	    region_device_offset(&probe) < region_device_offset(&store) + region_device_sz(&store) ||
	    region_device_offset(&probe) + 4096 > region_device_offset(&core))
		die("Q35 native service: invalid cold probe geometry\n");
	byte = (void *)(uintptr_t)(0x100000000ULL - media_size +
		region_device_offset(&probe) + 4095);
	if (*byte != 0xff)
		die("Q35 native service: probe scratch is not erased\n");
	/* Actual non-SMM denial observation, not a guest-readable SPI lock bit. */
	*byte = 0x40;
	*byte = 0x7f;
	*byte = 0xff;
	if (*byte != 0xff)
		die("Q35 native service: ordinary flash write was admitted\n");
	if (payload_mm_authvar_presence_tuple_sender_receipts_take(&receipts, &sender,
		&boot_private) != CB_SUCCESS ||
	    memcmp(&boot_private, &(struct bootmem_reservation_receipt) { 0 },
		sizeof(boot_private)) ||
	    !receipt_current(&receipts.page, BM_MEM_RESERVED, 4096) ||
	    !receipt_current(&receipts.service, BM_MEM_TABLE,
		PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE) ||
	    (uintptr_t)sender.page != receipts.page.base)
		die("Q35 native service: canonical receipt emission failed\n");
	frame = (void *)(uintptr_t)receipts.page.base;
	*frame = (struct q35_native_bootstrap_frame) {
		.revision = Q35_NATIVE_BOOTSTRAP_REVISION, .size = sizeof(*frame),
		.state = Q35_NATIVE_BOOTSTRAP_REQUEST, .receipts = receipts,
		.probe_offset = region_device_offset(&probe), .probe_size = 4096,
		.write_denied = 1,
	};
	wire = trigger(Q35_NATIVE_BOOTSTRAP_WIRE_REQUEST, (uintptr_t)frame);
	publication.response = *frame;
	if (wire != Q35_NATIVE_BOOTSTRAP_WIRE_SUCCESS ||
	    publication.response.revision != Q35_NATIVE_BOOTSTRAP_REVISION ||
	    publication.response.size != sizeof(*frame) ||
	    publication.response.state != Q35_NATIVE_BOOTSTRAP_COMPLETE ||
	    publication.response.reserved || publication.response.maximum_cpus != 1 ||
	    publication.response.write_denied != 1 ||
	    publication.response.probe_offset != region_device_offset(&probe) ||
	    publication.response.probe_size != 4096 ||
	    memcmp(&publication.response.receipts, &receipts, sizeof(receipts)) ||
	    payload_mm_authvar_service_endpoint_validate(&publication.response.endpoint) !=
		CB_SUCCESS ||
	    !(publication.response.endpoint.flags & LB_AUTHVAR_ENDPOINT_IMAGE_POLICY_GENERAL) ||
	    publication.response.endpoint.communication_base != receipts.service.base ||
	    publication.response.endpoint.communication_size != receipts.service.bytes ||
	    publication.response.endpoint.generation != receipts.service.generation ||
	    publication.response.endpoint.trigger_value != SMM_APMC_AUTHVAR_SERVICE)
		die("Q35 native service: genuine service completion failed\n");
	publication.record = (void *)lb_new_record(header);
	memcpy(publication.record, &publication.response.endpoint,
		sizeof(publication.response.endpoint));
	publication.header = header;
	publication.complete = true;
}

bool platform_payload_mm_authvar_service_published(const struct lb_header *header,
	uintptr_t table_end)
{
	const uintptr_t record = (uintptr_t)publication.record;

	return publication.complete && mainboard_authvar_presence_cold_boot() &&
		header && publication.header == header &&
		(uintptr_t)header <= UINTPTR_MAX - sizeof(*header) &&
		record >= (uintptr_t)header + sizeof(*header) && record < table_end &&
		sizeof(*publication.record) <= table_end - record &&
		!memcmp(publication.record, &publication.response.endpoint,
			sizeof(*publication.record)) &&
		receipt_current(&publication.response.receipts.page, BM_MEM_RESERVED, 4096) &&
		receipt_current(&publication.response.receipts.service, BM_MEM_TABLE,
			PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE);
}

bool platform_capsule_ram_persistent(const struct lb_header *header, uintptr_t table_end)
{
	/* Warm-reset retention is limited to this actual Q35 public-MM composition. */
	return CONFIG(Q35_SMM_INVOCATION_NATIVE_PUBLIC_SERVICE_COMPONENT) &&
		CONFIG(DRIVERS_EFI_CAPSULE_MM_READ_ADMISSION) &&
		CONFIG(DRIVERS_EFI_CAPSULE_RAM_HANDOFF) &&
		!acpi_is_wakeup_s3() &&
		(!CONFIG(PLATFORM_HAS_DRAM_CLEAR) || !security_clear_dram_request()) &&
		platform_payload_mm_authvar_service_published(header, table_end);
}
