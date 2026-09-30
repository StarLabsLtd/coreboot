/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#include <boot/payload_mm_authvar_presence_s3_backing.h>
#include <commonlib/bsd/ipchksum.h>
#include <commonlib/bsd/cbmem_id.h>
#include <cbmem.h>
#include <cpu/x86/smm_command.h>
#include <stdint.h>
#include <string.h>

struct cbmem_entry {
	uint8_t unused;
};

struct test_table {
	struct lb_header header;
	struct lb_authvar_presence_endpoint presence;
	struct lb_authvar_presence_lifecycle_close_endpoint close;
} __packed __aligned(4);

static bool allow_dram = true;
static struct smm_invocation_loader_instance loader_instance;
static struct test_table *hook_table;
static unsigned int mutate_hook;
static bool mutate_instance_read;
static struct test_table *cbtable;
static struct cbmem_entry fake_entry;

const struct cbmem_entry *cbmem_entry_find(uint32_t id)
{
	return id == CBMEM_ID_CBTABLE && cbtable ? &fake_entry : NULL;
}

void *cbmem_entry_start(const struct cbmem_entry *entry)
{
	return entry == &fake_entry ? cbtable : NULL;
}

uint64_t cbmem_entry_size(const struct cbmem_entry *entry)
{
	return entry == &fake_entry ? sizeof(*cbtable) : 0U;
}

void smm_invocation_loader_instance_test_hook(unsigned int point)
{
	if (mutate_instance_read && point == 3U) {
		loader_instance.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
		mutate_instance_read = false;
	}
}

void payload_mm_authvar_presence_s3_backing_test_hook(unsigned int point)
{
	if (hook_table && point == mutate_hook)
		hook_table->presence.generation++;
}

void mock_assert(const int result, const char *expression, const char *file,
	const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static bool dram_contains(uint64_t base, uint64_t size)
{
	return allow_dram &&
		size == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE &&
		(base == 0x100000U || base == 0x102000U);
}

bool bootmem_domain_dram_contains(uint64_t base, uint64_t size)
{
	return dram_contains(base, size);
}

static void finish(struct test_table *table)
{
	table->header.table_bytes = sizeof(*table) - sizeof(table->header);
	table->header.table_checksum = ipchksum(&table->presence,
		table->header.table_bytes);
	table->header.header_checksum = 0;
	table->header.header_checksum = ipchksum(&table->header,
		sizeof(table->header));
}

static void initialize(struct test_table *table)
{
	memset(table, 0, sizeof(*table));
	memcpy(table->header.signature, "LBIO", 4);
	table->header.header_bytes = sizeof(table->header);
	table->header.table_entries = 2;
	table->presence = (struct lb_authvar_presence_endpoint) {
		.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
		.size = sizeof(table->presence),
		.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
		.header_size = sizeof(table->presence),
		.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS,
		.generation = 7,
		.communication_base = 0x100000U,
		.communication_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
		.message_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
		.transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
		.trigger_width = 1,
		.trigger_address = 0xb2,
		.trigger_value = SMM_APMC_AUTHVAR_PRESENCE,
		.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
		.capability_size = LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
	};
	table->close = (struct lb_authvar_presence_lifecycle_close_endpoint) {
		.tag = LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT,
		.size = sizeof(table->close),
		.revision = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_REVISION,
		.header_size = sizeof(table->close),
		.flags = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_REQUIRED_FLAGS,
		.generation = 7,
		.communication_base = 0x102000U,
		.communication_size =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
		.message_size =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE,
		.transport = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT_APM_IO8,
		.trigger_width = 1,
		.trigger_address = 0xb2,
		.trigger_value = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		.source_mask = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK,
	};
	finish(table);
	loader_instance = (struct smm_invocation_loader_instance) {
		.state = SMM_INVOCATION_LOADER_INSTANCE_READY,
		.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION,
		.size = sizeof(loader_instance),
		.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD,
		.loader_instance_nonce = { .low = 11U, .high = 13U },
	};
	hook_table = NULL;
	mutate_hook = 0;
	mutate_instance_read = false;
	cbtable = NULL;
}

static bool parse(struct test_table *table)
{
	struct payload_mm_authvar_presence_s3_backing output;

	memset(&output, 0xa5, sizeof(output));
	return payload_mm_authvar_presence_s3_backing_from_table(table,
		sizeof(*table), dram_contains, &loader_instance, true, &output);
}

int main(void)
{
	struct test_table table;
	struct smm_invocation_loader_instance loader_snapshot;
	uint8_t partial_alias[sizeof(struct payload_mm_authvar_presence_s3_backing) +
		sizeof(struct smm_invocation_loader_instance)] __aligned(8);
	uint8_t partial_snapshot[sizeof(partial_alias)];
	struct smm_invocation_loader_instance *partial_loader;
	struct {
		struct lb_header header;
		struct lb_authvar_presence_endpoint presence;
		struct lb_authvar_presence_lifecycle_close_endpoint close[2];
	} __packed __aligned(4) duplicate;
	struct payload_mm_authvar_presence_s3_backing output;

	initialize(&table);
	assert(payload_mm_authvar_presence_s3_backing_from_table(&table,
		sizeof(table), dram_contains, &loader_instance, true, &output));
	assert(output.revision == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_REVISION);
	assert(output.size == sizeof(output));
	assert(output.presence.communication_base == 0x100000U);
	assert(output.lifecycle_close.communication_base == 0x102000U);
	assert(output.loader_lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD);
	assert(smm_invocation_loader_instance_nonce_equal(
		output.loader_instance_nonce, loader_instance.loader_instance_nonce));
	assert(payload_mm_authvar_presence_s3_backing_validate(&output,
		&loader_instance));
	assert(payload_mm_authvar_presence_s3_backing_dram_provenance(&output,
		&loader_instance, &output, 0x100000U, 4096U));
	assert(!payload_mm_authvar_presence_s3_backing_dram_provenance(&output,
		&loader_instance, &output, UINT64_MAX - 2047U, 4096U));
	cbtable = &table;
	assert(platform_smm_authvar_presence_s3_backing(&loader_instance, true,
		&output));
	cbtable = NULL;
	assert(!platform_smm_authvar_presence_s3_backing(&loader_instance, true,
		&output));

	/* Output may neither exactly nor partially alias protected loader identity. */
	initialize(&table);
	loader_snapshot = loader_instance;
	assert(!payload_mm_authvar_presence_s3_backing_from_table(&table,
		sizeof(table), dram_contains, &loader_instance, true,
		(void *)&loader_instance));
	assert(!memcmp(&loader_instance, &loader_snapshot,
		sizeof(loader_instance)));
	memset(partial_alias, 0xa5, sizeof(partial_alias));
	partial_loader = (void *)(partial_alias + 8U);
	*partial_loader = loader_instance;
	memcpy(partial_snapshot, partial_alias, sizeof(partial_snapshot));
	assert(!payload_mm_authvar_presence_s3_backing_from_table(&table,
		sizeof(table), dram_contains, partial_loader, true,
		(void *)partial_alias));
	assert(!memcmp(partial_alias, partial_snapshot, sizeof(partial_alias)));

	initialize(&table); table.header.signature[0] ^= 1; assert(!parse(&table));
	initialize(&table); table.header.header_checksum ^= 1; assert(!parse(&table));
	initialize(&table); table.header.table_checksum ^= 1; assert(!parse(&table));
	initialize(&table); table.header.table_entries = 1; finish(&table);
	assert(!parse(&table));
	initialize(&table); table.close.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT;
	finish(&table); assert(!parse(&table));
	initialize(&table); table.close.generation++; finish(&table);
	assert(!parse(&table));
	initialize(&table); table.close.communication_base = 0x100000U;
	finish(&table); assert(!parse(&table));
	initialize(&table); table.presence.communication_base += 8; finish(&table);
	assert(!parse(&table));
	initialize(&table); table.close.size -= 4; finish(&table);
	assert(!parse(&table));
	initialize(&table); allow_dram = false; assert(!parse(&table));
	allow_dram = true;
	initialize(&table); loader_instance.lifecycle =
		SMM_INVOCATION_LOADER_NON_S3_LOAD; assert(!parse(&table));
	initialize(&table); assert(!payload_mm_authvar_presence_s3_backing_from_table(
		&table, sizeof(table), dram_contains, &loader_instance, false, &output));
	initialize(&table); hook_table = &table; mutate_hook = 3U; assert(!parse(&table));
	initialize(&table); hook_table = &table; mutate_hook = 6U; assert(!parse(&table));
	initialize(&table); assert(!payload_mm_authvar_presence_s3_backing_from_table(
		&table, SIZE_MAX, dram_contains, &loader_instance, true, &output));
	initialize(&table); assert(!payload_mm_authvar_presence_s3_backing_from_table(
		&table, sizeof(table), dram_contains, &loader_instance, true,
		(void *)((uint8_t *)&table + offsetof(struct test_table, presence))));
	initialize(&table); table.close.communication_base = UINT64_MAX - 2047U;
	finish(&table); assert(!parse(&table));
	initialize(&table); table.header.table_bytes += 4U;
	table.header.header_checksum = 0;
	table.header.header_checksum = ipchksum(&table.header, sizeof(table.header));
	assert(!parse(&table));
	initialize(&table); assert(!payload_mm_authvar_presence_s3_backing_from_table(
		&table, sizeof(table) - 4U, dram_contains, &loader_instance, true,
		&output));
	initialize(&table); table.close.size += 4U; finish(&table);
	assert(!parse(&table));
	initialize(&table);
	memset(&duplicate, 0, sizeof(duplicate));
	duplicate.header = table.header;
	duplicate.header.table_entries = 3U;
	duplicate.header.table_bytes = sizeof(duplicate) - sizeof(duplicate.header);
	duplicate.presence = table.presence;
	duplicate.close[0] = duplicate.close[1] = table.close;
	duplicate.header.table_checksum = ipchksum(&duplicate.presence,
		duplicate.header.table_bytes);
	duplicate.header.header_checksum = 0;
	duplicate.header.header_checksum = ipchksum(&duplicate.header,
		sizeof(duplicate.header));
	assert(!payload_mm_authvar_presence_s3_backing_from_table(&duplicate,
		sizeof(duplicate), dram_contains, &loader_instance, true, &output));
	initialize(&table); mutate_instance_read = true; assert(!parse(&table));
	initialize(&table); assert(payload_mm_authvar_presence_s3_backing_from_table(
		&table, sizeof(table), dram_contains, &loader_instance, true, &output));
	output.loader_instance_nonce.low++;
	assert(!payload_mm_authvar_presence_s3_backing_validate(&output,
		&loader_instance));
	output.loader_instance_nonce.low--;
	output.presence.communication_base = UINT64_MAX - 2047U;
	assert(!payload_mm_authvar_presence_s3_backing_validate(&output,
		&loader_instance));
	return 0;
}
