/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_platform_facts.h>
#include <boot_device.h>
#include <pthread.h>
#include <sched.h>
#include <spi_flash.h>
#include <stdlib.h>
#include <string.h>

#include "capsule_platform_adapter_internal.h"

extern int dprintf(int fd, const char *format, ...);

static uint8_t flash_bytes[64 * 1024];
static uint8_t staging[64 * 1024];
static uint8_t protected_memory[4096];
static struct region_device read_only;
static struct region_device read_write;
static struct spi_flash flash;
static bool media_available = true;
static bool short_read;
static bool short_write;
static bool short_erase;
static uint32_t reenter;
static int nested_status;
static struct capsule_platform_facts *hook_facts;
static uint32_t hold_builder;
static uint32_t builder_entered;

static const struct region_device_ops media_ops = {
	.readat = rdev_readat,
	.writeat = rdev_writeat,
	.eraseat = rdev_eraseat,
};

void capsule_platform_adapter_test_acquired(void)
{
	if (reenter == 1) {
		struct capsule_platform_media_context context;
		struct capsule_media_backend backend;

		reenter = 0;
		nested_status = capsule_platform_media_adapter_build(hook_facts,
			&context, &backend);
	} else if (reenter == 2) {
		struct capsule_platform_identity identity;

		reenter = 0;
		nested_status = capsule_platform_identity_build(hook_facts,
			&identity);
	}
	if (__atomic_load_n(&hold_builder, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&builder_entered, 1, __ATOMIC_RELEASE);
		while (__atomic_load_n(&hold_builder, __ATOMIC_ACQUIRE))
			sched_yield();
	}
}

bool capsule_platform_adapter_test_workspace_clear(void);

void mock_assert(const int value, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	if (!value) {
		dprintf(2, "%s:%d: assertion failed: %s\n", file, line,
			expression);
		abort();
	}
}

void boot_device_init(void) {}

const struct region_device *boot_device_ro(void)
{
	return media_available ? &read_only : NULL;
}

const struct region_device *boot_device_rw(void)
{
	return media_available ? &read_write : NULL;
}

const struct spi_flash *boot_device_spi_flash(void)
{
	return media_available ? &flash : NULL;
}

ssize_t rdev_readat(const struct region_device *device, void *buffer,
	size_t offset, size_t size)
{
	if (device != &read_write || offset > sizeof(flash_bytes) ||
	    size > sizeof(flash_bytes) - offset)
		return -1;
	memcpy(buffer, flash_bytes + offset, size);
	return short_read ? (ssize_t)size - 1 : (ssize_t)size;
}

ssize_t rdev_writeat(const struct region_device *device, const void *buffer,
	size_t offset, size_t size)
{
	if (device != &read_write || offset > sizeof(flash_bytes) ||
	    size > sizeof(flash_bytes) - offset)
		return -1;
	memcpy(flash_bytes + offset, buffer, size);
	return short_write ? (ssize_t)size - 1 : (ssize_t)size;
}

ssize_t rdev_eraseat(const struct region_device *device, size_t offset,
	size_t size)
{
	if (device != &read_write || offset > sizeof(flash_bytes) ||
	    size > sizeof(flash_bytes) - offset)
		return -1;
	memset(flash_bytes + offset, 0xff, size);
	return short_erase ? (ssize_t)size - 1 : (ssize_t)size;
}

int region_is_subregion(const struct region *parent,
	const struct region *child)
{
	return parent->size && child->size && child->offset >= parent->offset &&
		child->offset - parent->offset <= parent->size &&
		child->size <= parent->size - (child->offset - parent->offset);
}

void smm_region(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)protected_memory;
	*size = sizeof(protected_memory);
}

static void initialize_facts(struct capsule_platform_facts *facts)
{
	read_only = (struct region_device)REGION_DEV_INIT(&media_ops, 0,
		sizeof(flash_bytes));
	read_write = (struct region_device)REGION_DEV_INIT(&media_ops, 0,
		sizeof(flash_bytes));
	flash = (struct spi_flash) {
		.size = sizeof(flash_bytes),
		.page_size = 256,
		.sector_size = 4096,
	};
	media_available = true;
	short_read = false;
	short_write = false;
	short_erase = false;
	*facts = (struct capsule_platform_facts) {
		.revision = CAPSULE_PLATFORM_FACTS_REVISION,
		.size = sizeof(*facts),
		.boot_media_size = sizeof(flash_bytes),
		.block_size = 256,
		.erase_size = 4096,
		.buffers = {
			.communication_base = (uintptr_t)protected_memory - 0x2000,
			.communication_reserved_size = 4096,
			.communication_size = CAPSULE_BROKER_TRANSPORT_SIZE,
			.staging_base = (uintptr_t)staging,
			.staging_size = sizeof(staging),
		},
		.scratch = {
			.write_base = (uintptr_t)protected_memory,
			.read_base = (uintptr_t)protected_memory + 4096,
			.erase_size = 4096,
		},
		.firmware = {
			.tag = LB_TAG_EFI_FW_INFO,
			.size = sizeof(struct lb_efi_fw_info),
			.guid = { 0x37, 0x48, 0x58, 0x85, 0x03, 0x7b, 0x6c, 0x4b,
				  0x94, 0x0c, 0x8b, 0x61, 0x86, 0xcb, 0xf7, 0xa1 },
			.version = 0x001a0009,
			.lowest_supported_version = 0x001a0008,
			.fw_size = sizeof(flash_bytes),
		},
		.fmap = {
			.size = sizeof(flash_bytes),
			.area_count = 1,
			.area = { { .offset = 0, .size = sizeof(flash_bytes) } },
		},
	};
}

static void test_media(void)
{
	struct capsule_platform_media_context context;
	struct capsule_platform_media_context saved_context;
	struct capsule_media_backend backend;
	struct capsule_platform_facts facts;
	uint8_t block[4096];
	uint8_t output[4096];

	initialize_facts(&facts);
	memset(block, 0x5a, sizeof(block));
	assert(capsule_platform_media_adapter_build(&facts, &context,
		&backend) == CB_SUCCESS);
	saved_context = context;
	assert(capsule_platform_adapter_test_workspace_clear());
	assert(backend.context == &context);
	assert(backend.source_valid(&context, staging, sizeof(staging)));
	assert(backend.source_valid(&context, staging + 1,
		sizeof(staging) - 1));
	assert(!backend.source_valid(&context,
		(void *)((uintptr_t)staging - 1), 1));
	assert(!backend.source_valid(&context, staging + sizeof(staging), 1));
	assert(backend.erase(&context, 0, sizeof(block)) == CB_SUCCESS);
	assert(backend.write(&context, 0, block, sizeof(block)) == CB_SUCCESS);
	assert(backend.sync(&context) == CB_SUCCESS);
	assert(backend.read(&context, 0, output, sizeof(output)) == CB_SUCCESS);
	assert(!memcmp(block, output, sizeof(block)));
	assert(backend.erase(&context, 1, sizeof(block)) == CB_ERR);
	assert(backend.write(&context, 1, block, sizeof(block)) == CB_ERR);
	assert(backend.read(&context, sizeof(flash_bytes), output, 1) == CB_ERR);
	short_read = true;
	assert(backend.read(&context, 0, output, sizeof(output)) == CB_ERR);
	short_read = false;
	short_write = true;
	assert(backend.write(&context, 0, block, sizeof(block)) == CB_ERR);
	short_write = false;
	short_erase = true;
	assert(backend.erase(&context, 0, sizeof(block)) == CB_ERR);
	short_erase = false;
	flash.sector_size = 8192;
	assert(backend.sync(&context) == CB_ERR);
	flash.sector_size = 4096;
	media_available = false;
	assert(backend.sync(&context) == CB_ERR);
	media_available = true;
	read_only.region.offset = 1;
	assert(backend.sync(&context) == CB_ERR);
	read_only.region.offset = 0;
	read_write.region.offset = 1;
	assert(backend.sync(&context) == CB_ERR);
	read_write.region.offset = 0;
	context.erase_size = 0;
	assert(backend.erase(&context, 0, sizeof(block)) == CB_ERR);
	context = saved_context;
	context.block_size = 0;
	assert(backend.write(&context, 0, block, sizeof(block)) == CB_ERR);
	context = saved_context;
	context.staging_base = UINTPTR_MAX - 1;
	context.staging_size = 4;
	assert(!backend.source_valid(&context,
		(void *)(uintptr_t)(UINTPTR_MAX - 1), 4));
	context = saved_context;
	assert(!backend.source_valid(&context,
		(void *)(uintptr_t)(UINTPTR_MAX - 1), 4));
}

static void test_build_failures(void)
{
	struct capsule_platform_media_context context;
	struct capsule_media_backend backend;
	struct capsule_platform_facts facts;

	initialize_facts(&facts);
	facts.firmware.fw_size--;
	memset(&context, 0xa5, sizeof(context));
	memset(&backend, 0xa5, sizeof(backend));
	assert(capsule_platform_media_adapter_build(&facts, &context,
		&backend) == CB_ERR);
	assert(capsule_platform_adapter_test_workspace_clear());
	for (size_t i = 0; i < sizeof(context); i++)
		assert(((const uint8_t *)&context)[i] == 0);
	for (size_t i = 0; i < sizeof(backend); i++)
		assert(((const uint8_t *)&backend)[i] == 0);
	initialize_facts(&facts);
	facts.buffers.staging_base = UINTPTR_MAX - facts.buffers.staging_size + 1;
	assert(capsule_platform_media_adapter_build(&facts, &context,
		&backend) == CB_ERR);
	initialize_facts(&facts);
	assert(capsule_platform_media_adapter_build(&facts,
		(void *)&facts, &backend) == CB_ERR);
	assert(capsule_platform_media_adapter_build(&facts, &context,
		(void *)&facts) == CB_ERR);
}

static void test_storage(void)
{
	assert(capsule_platform_smm_storage_contains(NULL,
		protected_memory, sizeof(protected_memory)));
	assert(capsule_platform_smm_storage_contains(NULL,
		protected_memory + 1, sizeof(protected_memory) - 1));
	assert(!capsule_platform_smm_storage_contains(NULL,
		(void *)((uintptr_t)protected_memory - 1), 2));
	assert(!capsule_platform_smm_storage_contains(NULL,
		protected_memory + sizeof(protected_memory) - 1, 2));
	assert(!capsule_platform_smm_storage_contains((void *)1,
		protected_memory, 1));
	assert(!capsule_platform_smm_storage_contains(NULL,
		protected_memory, 0));
}

static void *concurrent_builder(void *argument)
{
	struct capsule_platform_identity identity;

	return (void *)(uintptr_t)capsule_platform_identity_build(argument,
		&identity);
}

static void test_serialization(void)
{
	struct capsule_platform_media_context context;
	struct capsule_media_backend backend;
	struct capsule_platform_facts facts;
	pthread_t thread;
	void *thread_status;

	initialize_facts(&facts);
	hook_facts = &facts;
	reenter = 1;
	nested_status = CB_SUCCESS;
	assert(capsule_platform_media_adapter_build(&facts, &context,
		&backend) == CB_SUCCESS);
	assert(nested_status == CB_ERR);
	assert(capsule_platform_adapter_test_workspace_clear());
	reenter = 2;
	nested_status = CB_SUCCESS;
	assert(capsule_platform_identity_build(&facts,
		&(struct capsule_platform_identity){ 0 }) == CB_SUCCESS);
	assert(nested_status == CB_ERR);
	assert(capsule_platform_adapter_test_workspace_clear());
	__atomic_store_n(&hold_builder, 1, __ATOMIC_RELEASE);
	__atomic_store_n(&builder_entered, 0, __ATOMIC_RELEASE);
	assert(!pthread_create(&thread, NULL, concurrent_builder, &facts));
	while (!__atomic_load_n(&builder_entered, __ATOMIC_ACQUIRE))
		sched_yield();
	assert(capsule_platform_media_adapter_build(&facts, &context,
		&backend) == CB_ERR);
	__atomic_store_n(&hold_builder, 0, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, &thread_status));
	assert((uintptr_t)thread_status == CB_SUCCESS);
	assert(capsule_platform_adapter_test_workspace_clear());
}

static void test_identity(void)
{
	struct capsule_platform_identity identity;
	struct capsule_platform_facts facts;

	initialize_facts(&facts);
	assert(capsule_platform_identity_build(&facts, &identity) == CB_SUCCESS);
	assert(capsule_platform_adapter_test_workspace_clear());
	assert(identity.state.revision == PAYLOAD_MM_FMP_STATE_POLICY_REVISION);
	assert(identity.state.hardware_instance == 0);
	assert(identity.state.trusted_lowest_version ==
		facts.firmware.lowest_supported_version);
	assert(!memcmp(identity.state.namespace_guid.b, facts.firmware.guid, 16));
	assert(identity.info.current_version == facts.firmware.version);
	assert(identity.info.image_size == facts.firmware.fw_size);
	assert(identity.info.capabilities ==
		LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES);
	assert(identity.authentication.mainboard_vendor_size == 9);
	assert(identity.authentication.mainboard_part_size == 15);
	assert(!memcmp(identity.authentication.mainboard_vendor,
		"Star Labs", 9));
	assert(!memcmp(identity.authentication.mainboard_part,
		"StarBook Mk VII", 15));
	facts.firmware.version = 0;
	memset(&identity, 0xa5, sizeof(identity));
	assert(capsule_platform_identity_build(&facts, &identity) == CB_ERR);
	assert(capsule_platform_adapter_test_workspace_clear());
	for (size_t i = 0; i < sizeof(identity); i++)
		assert(((const uint8_t *)&identity)[i] == 0);
}

int main(void)
{
	test_media();
	test_build_failures();
	test_storage();
	test_identity();
	test_serialization();
	return 0;
}
