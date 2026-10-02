/* SPDX-License-Identifier: GPL-2.0-only */

#include <types.h>
#include <string.h>
#include <boot/capsule_broker_buffers.h>
#include <boot/payload_mm_authvar_service.h>
#include <bootmem.h>
#include "../../src/mainboard/emulation/qemu-q35/native_service.h"

extern int dprintf(int descriptor, const char *format, ...);
#define check(condition) do { \
	if (!(condition)) { \
		dprintf(2, "PUBLICATION_ASSERT: %s\n", #condition); \
		__builtin_abort(); \
	} \
} while (0)

/* HOST callbacks model dependencies, never SMM or hardware authority. */
static struct {
	bool complete;
	const struct lb_header *header;
	struct lb_authvar_service_endpoint *record;
	struct q35_native_bootstrap_frame response;
	uint8_t capsule_handoff[sizeof(struct lb_capsule_handoff) +
		sizeof(struct lb_capsule_update_region)];
	struct lb_efi_fw_info firmware;
} publication;
static struct {
	struct lb_header header;
	struct lb_authvar_service_endpoint record;
} table;
static struct capsule_broker_buffer_reservation owned_buffers;
static struct lb_efi_fw_info actual_firmware;
static bool cold, receipts, buffers_current;
static enum cb_err handoff_status, endpoint_status;

bool mainboard_authvar_presence_cold_boot(void)
{
	return cold;
}
enum cb_err efi_fw_info_get(struct lb_efi_fw_info *firmware)
{
	*firmware = actual_firmware;
	return CB_SUCCESS;
}
bool capsule_broker_buffers_get(struct capsule_broker_buffer_reservation *buffers)
{
	*buffers = owned_buffers;
	return buffers_current;
}
static bool receipt_current(const struct bootmem_reservation_receipt *receipt,
	enum bootmem_type tag, uint64_t size)
{
	(void)receipt;
	(void)tag;
	(void)size;
	return receipts;
}
static enum cb_err capsule_handoff_validate(const struct lb_capsule_handoff *handoff,
	size_t size, const struct lb_efi_fw_info *firmware)
{
	(void)handoff;
	(void)size;
	(void)firmware;
	return handoff_status;
}
static enum cb_err capsule_broker_endpoint_validate(
	const struct lb_capsule_broker_endpoint *endpoint,
	const struct lb_capsule_handoff *handoff)
{
	(void)endpoint;
	(void)handoff;
	return endpoint_status;
}

#include Q35_PUBLICATION_SOURCE

static bool private_current;
static unsigned int closes;
static bool claim_current(void)
{
	return private_current;
}
static void capsule_broker_close_for_s3(void)
{
	closes++;
}
static enum cb_err completion(struct q35_native_bootstrap_frame *frame,
	struct q35_native_bootstrap_frame request)
{
	(void)frame;
	(void)request;
#include Q35_COMPLETION_SOURCE
	return CB_SUCCESS;
}

static enum cb_err ready(void)
{
	return capsule_publication_ready(&publication.response.capsule_endpoint,
		(const void *)publication.capsule_handoff,
		sizeof(publication.capsule_handoff), &publication.firmware);
}

static void reset(void)
{
	memset(&publication, 0, sizeof(publication));
	memset(&table, 0, sizeof(table));
	memset(&owned_buffers, 0, sizeof(owned_buffers));
	memset(&actual_firmware, 0, sizeof(actual_firmware));
	cold = receipts = buffers_current = true;
	handoff_status = endpoint_status = CB_SUCCESS;
	publication.complete = true;
	publication.header = &table.header;
	publication.record = &table.record;
	table.header.header_bytes = sizeof(table.header);
	publication.response.endpoint.generation = 71;
	table.record = publication.response.endpoint;
	owned_buffers.communication_base = 0x100000;
	owned_buffers.communication_size = 168;
	owned_buffers.communication_reserved_size = 4096;
	owned_buffers.staging_base = 0x200000;
	owned_buffers.staging_size = 0x900000;
	struct lb_capsule_broker_endpoint *endpoint = &publication.response.capsule_endpoint;
	endpoint->revision = LB_CAPSULE_BROKER_ENDPOINT_RAM_REVISION;
	endpoint->generation = publication.response.endpoint.generation;
	endpoint->communication_base = owned_buffers.communication_base;
	endpoint->communication_size = owned_buffers.communication_size;
	endpoint->staging_base = owned_buffers.staging_base;
	endpoint->staging_size = owned_buffers.staging_size;
	check(ready() == CB_SUCCESS);
}

int main(void)
{
	reset();
	table.header.table_bytes = sizeof(table.record);
	bool late_record_refused = ready() == CB_ERR;
	check(late_record_refused);
	reset();
	table.header.header_bytes--;
	check(ready() == CB_ERR);
	reset();
	table.record.generation++;
	check(ready() == CB_ERR);
	reset();
	publication.response.capsule_endpoint.generation++;
	check(ready() == CB_ERR);
	reset();
	owned_buffers.staging_size--;
	check(ready() == CB_ERR);
	reset();
	owned_buffers.communication_reserved_size = 1;
	check(ready() == CB_ERR);
	reset();
	actual_firmware.version++;
	check(ready() == CB_ERR);
	reset(); cold = false; check(ready() == CB_ERR);
	reset(); receipts = false; check(ready() == CB_ERR);
	reset(); buffers_current = false; check(ready() == CB_ERR);
	reset(); handoff_status = CB_ERR; check(ready() == CB_ERR);
	reset(); endpoint_status = CB_ERR; check(ready() == CB_ERR);
	reset(); publication.complete = false; check(ready() == CB_ERR);
	struct q35_native_bootstrap_frame request = {0}, frame = {0};
	private_current = true;
	check(completion(&frame, request) == CB_SUCCESS && closes == 0);
	for (size_t i = 0; i < sizeof(frame); i++) {
		((uint8_t *)&frame)[i] = 1;
		bool request_mutation_refused = completion(&frame, request) == CB_ERR;
		check(request_mutation_refused);
		check(closes == i + 1);
		((uint8_t *)&frame)[i] = 0;
	}
	private_current = false;
	check(completion(&frame, request) == CB_ERR && closes == sizeof(frame) + 1);
	return 0;
}
