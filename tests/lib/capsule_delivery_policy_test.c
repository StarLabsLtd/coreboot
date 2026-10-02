/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_delivery_policy.h>
#include <boot/coreboot_tables.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) __builtin_abort(); } while (0)

static struct lb_header table;
static struct lb_capsule_delivery_policy record;
static unsigned int records;
static unsigned int persistence_calls;

#ifdef TEST_RAM_PERSISTENCE_PROVIDER
bool platform_capsule_ram_persistent(void)
{
	persistence_calls++;
	return TEST_RAM_PERSISTENCE_PROVIDER;
}
#endif

struct lb_record *lb_new_record(struct lb_header *header)
{
	assert(header == &table);
	records++;
	return (void *)&record;
}

int main(void)
{
	const uint32_t expected_transports = TEST_EXPECTED_TRANSPORTS;

	memset(&record, 0xa5, sizeof(record));
	lb_add_capsule_delivery_policy(&table);
	assert(records == 1);
	assert(record.tag == LB_TAG_CAPSULE_DELIVERY_POLICY);
	assert(record.size == sizeof(record));
	assert(record.revision == LB_CAPSULE_DELIVERY_POLICY_REVISION);
	assert(record.header_size == sizeof(record));
	assert(record.allowed_transports == expected_transports);
	assert(record.max_nonpopulate == (expected_transports ?
		CONFIG_DRIVERS_EFI_CAPSULE_MAX_NONPOPULATE_SIZE : 0));
	assert(record.max_populate == (expected_transports ?
		CONFIG_DRIVERS_EFI_CAPSULE_MAX_POPULATE_SIZE : 0));
	assert(!record.reserved[0] && !record.reserved[1]);
#ifdef TEST_RAM_PERSISTENCE_PROVIDER
	assert(persistence_calls == CONFIG(DRIVERS_EFI_CAPSULE_RAM_HANDOFF));
#else
	assert(persistence_calls == 0);
	assert(!platform_capsule_ram_persistent());
#endif
	return 0;
}
