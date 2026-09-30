/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#include <boot/payload_mm_authvar_presence_s3_backing.h>
#include <commonlib/bsd/ipchksum.h>
#include <commonlib/helpers.h>
#include <string.h>

#if ENV_TEST
#define TEST_HOOK(point) payload_mm_authvar_presence_s3_backing_test_hook(point)
#else
#define TEST_HOOK(point) do { } while (0)
#endif

static void scrub(void *object, size_t size)
{
	volatile uint8_t *byte = object;
	while (size--)
		*byte++ = 0;
	__asm__ __volatile__("" : : "r" (byte) : "memory");
}

static bool span_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;
	return object && size && alignment && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool spans_disjoint(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;
	return span_valid(first, first_size, 1U) &&
		span_valid(second, second_size, 1U) &&
		(first_base + first_size - 1U < second_base ||
		 second_base + second_size - 1U < first_base);
}

static bool pages_disjoint(uint64_t first, uint64_t second)
{
	const uint64_t page_size = PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE;
	return first <= UINT64_MAX - (page_size - 1U) &&
		second <= UINT64_MAX - (page_size - 1U) &&
		(first + page_size - 1U < second ||
		 second + page_size - 1U < first);
}

static bool header_valid(const struct lb_header *header, size_t capacity)
{
	return capacity >= sizeof(*header) &&
		!memcmp(header->signature, "LBIO", sizeof(header->signature)) &&
		header->header_bytes == sizeof(*header) &&
		!ipchksum(header, sizeof(*header)) &&
		IS_ALIGNED(header->table_bytes, LB_ENTRY_ALIGN) &&
		header->table_bytes <= capacity - sizeof(*header);
}

static bool parse_once(const void *table, size_t capacity,
	struct payload_mm_authvar_presence_s3_backing *backing)
{
	const uint8_t *bytes = table;
	struct lb_header header;
	size_t offset = sizeof(header);
	uint32_t entries = 0;
	bool presence_found = false;
	bool close_found = false;

	memcpy(&header, bytes, sizeof(header));
	TEST_HOOK(1);
	if (!header_valid(&header, capacity) || memcmp(bytes, &header, sizeof(header)) ||
	    ipchksum(bytes + offset, header.table_bytes) != header.table_checksum)
		return false;
	while (offset - sizeof(header) < header.table_bytes) {
		struct lb_record record;
		const size_t remaining = header.table_bytes - (offset - sizeof(header));

		if (remaining < sizeof(record))
			return false;
		memcpy(&record, bytes + offset, sizeof(record));
		TEST_HOOK(2);
		if (memcmp(bytes + offset, &record, sizeof(record)) ||
		    record.size < sizeof(record) || !IS_ALIGNED(record.size, LB_ENTRY_ALIGN) ||
		    record.size > remaining)
			return false;
		if (record.tag == LB_TAG_AUTHVAR_PRESENCE_ENDPOINT) {
			struct lb_authvar_presence_endpoint endpoint;
			if (presence_found || record.size != sizeof(endpoint))
				return false;
			memcpy(&endpoint, bytes + offset, sizeof(endpoint));
			TEST_HOOK(3);
			if (memcmp(bytes + offset, &endpoint, sizeof(endpoint)))
				return false;
			backing->presence = endpoint;
			scrub(&endpoint, sizeof(endpoint));
			presence_found = true;
		} else if (record.tag ==
			LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT) {
			struct lb_authvar_presence_lifecycle_close_endpoint endpoint;
			if (close_found || record.size != sizeof(endpoint))
				return false;
			memcpy(&endpoint, bytes + offset, sizeof(endpoint));
			TEST_HOOK(4);
			if (memcmp(bytes + offset, &endpoint, sizeof(endpoint)))
				return false;
			backing->lifecycle_close = endpoint;
			scrub(&endpoint, sizeof(endpoint));
			close_found = true;
		}
		offset += record.size;
		if (entries == UINT32_MAX)
			return false;
		entries++;
	}
	TEST_HOOK(5);
	return offset - sizeof(header) == header.table_bytes &&
		entries == header.table_entries && presence_found && close_found &&
		ipchksum(bytes + sizeof(header), header.table_bytes) ==
			header.table_checksum && !memcmp(bytes, &header, sizeof(header));
}

bool payload_mm_authvar_presence_s3_backing_validate(
	const struct payload_mm_authvar_presence_s3_backing *backing,
	const struct smm_invocation_loader_instance *loader_instance)
{
	struct smm_invocation_loader_instance instance = { 0 };
	struct smm_invocation_loader_instance check = { 0 };
	const uint64_t page_size = PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE;
	bool valid = false;

	if (!span_valid(backing, sizeof(*backing), _Alignof(*backing)) ||
	    !span_valid(loader_instance, sizeof(*loader_instance),
		_Alignof(*loader_instance)) ||
	    !spans_disjoint(backing, sizeof(*backing), loader_instance,
		sizeof(*loader_instance)) ||
	    smm_invocation_loader_instance_read(loader_instance, &instance) != CB_SUCCESS)
		goto out;
	valid = backing->revision == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_REVISION &&
		backing->size == sizeof(*backing) && !backing->reserved &&
		backing->loader_lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD &&
		instance.lifecycle == backing->loader_lifecycle &&
		smm_invocation_loader_instance_nonce_equal(instance.loader_instance_nonce,
			backing->loader_instance_nonce) &&
		payload_mm_authvar_presence_endpoint_validate(&backing->presence) ==
			CB_SUCCESS &&
		payload_mm_authvar_presence_lifecycle_close_endpoint_validate(
			&backing->lifecycle_close) == CB_SUCCESS &&
		backing->presence.generation == backing->lifecycle_close.generation &&
		IS_ALIGNED(backing->presence.communication_base, page_size) &&
		IS_ALIGNED(backing->lifecycle_close.communication_base, page_size) &&
		pages_disjoint(backing->presence.communication_base,
			backing->lifecycle_close.communication_base) &&
		smm_invocation_loader_instance_read(loader_instance, &check) == CB_SUCCESS &&
		!memcmp(&instance, &check, sizeof(instance));
out:
	scrub(&instance, sizeof(instance));
	scrub(&check, sizeof(check));
	return valid;
}

bool payload_mm_authvar_presence_s3_backing_dram_provenance(
	const struct payload_mm_authvar_presence_s3_backing *backing,
	const struct smm_invocation_loader_instance *loader_instance,
	void *context, uint64_t base, uint64_t size)
{
	return context == backing &&
		payload_mm_authvar_presence_s3_backing_validate(backing, loader_instance) &&
		size == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE &&
		base <= UINT64_MAX - (size - 1U) &&
		(base == backing->presence.communication_base ||
		 base == backing->lifecycle_close.communication_base);
}

bool payload_mm_authvar_presence_s3_backing_from_table(
	const void *table, size_t table_capacity,
	payload_mm_authvar_presence_s3_dram_contains_fn dram_contains,
	const struct smm_invocation_loader_instance *loader_instance,
	bool acpi_s3, struct payload_mm_authvar_presence_s3_backing *backing)
{
	struct payload_mm_authvar_presence_s3_backing first = { 0 };
	struct payload_mm_authvar_presence_s3_backing second = { 0 };
	struct smm_invocation_loader_instance instance = { 0 };
	struct smm_invocation_loader_instance check = { 0 };
	bool valid = false;

	/* Reject every invalid or aliased caller span before touching output. */
	if (!span_valid(backing, sizeof(*backing), _Alignof(*backing)) ||
	    !span_valid(table, table_capacity, LB_ENTRY_ALIGN) ||
	    !span_valid(loader_instance, sizeof(*loader_instance),
		_Alignof(*loader_instance)) ||
	    !spans_disjoint(table, table_capacity, backing, sizeof(*backing)) ||
	    !spans_disjoint(table, table_capacity, loader_instance,
		sizeof(*loader_instance)) ||
	    !spans_disjoint(backing, sizeof(*backing), loader_instance,
		sizeof(*loader_instance)))
		return false;
	memset(backing, 0, sizeof(*backing));
	if (!dram_contains || !acpi_s3 ||
	    smm_invocation_loader_instance_read(loader_instance, &instance) != CB_SUCCESS ||
	    instance.lifecycle != SMM_INVOCATION_LOADER_S3_RELOAD)
		goto out;
	first.loader_lifecycle = second.loader_lifecycle = instance.lifecycle;
	first.loader_instance_nonce = second.loader_instance_nonce =
		instance.loader_instance_nonce;
	if (!parse_once(table, table_capacity, &first) ||
	    !dram_contains(first.presence.communication_base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE) ||
	    !dram_contains(first.lifecycle_close.communication_base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE))
		goto out;
	TEST_HOOK(6);
	if (!parse_once(table, table_capacity, &second) ||
	    memcmp(&first, &second, sizeof(first)) ||
	    !dram_contains(second.presence.communication_base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE) ||
	    !dram_contains(second.lifecycle_close.communication_base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE) ||
	    smm_invocation_loader_instance_read(loader_instance, &check) != CB_SUCCESS ||
	    memcmp(&instance, &check, sizeof(instance)))
		goto out;
	first.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_REVISION;
	first.size = sizeof(first);
	if (!payload_mm_authvar_presence_s3_backing_validate(&first, loader_instance))
		goto out;
	*backing = first;
	valid = true;
out:
	if (!valid)
		memset(backing, 0, sizeof(*backing));
	scrub(&first, sizeof(first));
	scrub(&second, sizeof(second));
	scrub(&instance, sizeof(instance));
	scrub(&check, sizeof(check));
	return valid;
}
