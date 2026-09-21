/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <commonlib/region.h>
#include <cpu/x86/smm.h>
#include <identity.h>
#include <spi_flash.h>
#include <string.h>

#include "capsule_platform_adapter_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Capsule platform adapters must only be built in SMM"
#endif

_Static_assert(sizeof(struct capsule_platform_media_context) <= 128U,
	"capsule media context exceeds broker callback-context limit");

static struct {
	uint32_t busy;
	struct capsule_platform_facts facts;
} build_workspace;

#if ENV_TEST
void capsule_platform_adapter_test_acquired(void);
#endif

static bool build_acquire(void)
{
	uint32_t expected = 0;

	if (!__atomic_compare_exchange_n(&build_workspace.busy, &expected, 1,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return false;
#if ENV_TEST
	capsule_platform_adapter_test_acquired();
#endif
	return true;
}

static void build_release(void)
{
	memset(&build_workspace.facts, 0, sizeof(build_workspace.facts));
	__atomic_store_n(&build_workspace.busy, 0, __ATOMIC_RELEASE);
}

#if ENV_TEST
bool capsule_platform_adapter_test_workspace_clear(void)
{
	const uint8_t *bytes = (const void *)&build_workspace.facts;
	uint8_t value = 0;

	for (size_t i = 0; i < sizeof(build_workspace.facts); i++)
		value |= bytes[i];
	return !__atomic_load_n(&build_workspace.busy, __ATOMIC_ACQUIRE) &&
		value == 0;
}
#endif

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_base = (uintptr_t)left;
	uintptr_t right_base = (uintptr_t)right;

	if (!left_size || !right_size || left_base > UINTPTR_MAX - left_size ||
	    right_base > UINTPTR_MAX - right_size)
		return true;
	return left_base < right_base + right_size &&
		right_base < left_base + left_size;
}

static bool range_valid(uint64_t offset, size_t size, uint64_t limit)
{
	return size && offset <= limit && size <= limit - offset;
}

static bool bytes_present(const uint8_t *bytes, size_t size)
{
	uint8_t value = 0;

	for (size_t i = 0; i < size; i++)
		value |= bytes[i];
	return value != 0;
}

static bool media_context_valid(
	const struct capsule_platform_media_context *context)
{
	return context && context->media_size && context->media_size <= SIZE_MAX &&
		context->block_size && context->erase_size &&
		context->media_size % context->erase_size == 0 &&
		context->erase_size % context->block_size == 0 &&
		context->staging_base <= UINTPTR_MAX &&
		context->staging_size && context->staging_size <= SIZE_MAX &&
		(uintptr_t)context->staging_base <=
			UINTPTR_MAX - (size_t)context->staging_size;
}

static bool media_context_snapshot(const void *opaque,
	struct capsule_platform_media_context *snapshot)
{
	if (!opaque || !snapshot)
		return false;
	memcpy(snapshot, opaque, sizeof(*snapshot));
	return media_context_valid(snapshot) &&
		!memcmp(opaque, snapshot, sizeof(*snapshot));
}

static bool media_current(const struct capsule_platform_media_context *context,
	const struct region_device **media)
{
	const struct region_device *read_only;
	const struct region_device *read_write;
	const struct region_device *root;
	const struct spi_flash *flash;

	if (!media || !media_context_valid(context))
		return false;
	boot_device_init();
	read_only = boot_device_ro();
	read_write = boot_device_rw();
	flash = boot_device_spi_flash();
	root = read_write && read_write->root ? read_write->root : read_write;
	if (!read_only || !read_write || !flash ||
	    !root || !root->ops || !root->ops->readat ||
	    !root->ops->writeat || !root->ops->eraseat ||
	    region_device_offset(read_only) || region_device_offset(read_write) ||
	    region_device_sz(read_only) != context->media_size ||
	    region_device_sz(read_write) != context->media_size ||
	    flash->size != context->media_size ||
	    flash->page_size != context->block_size ||
	    flash->sector_size != context->erase_size)
		return false;
	*media = read_write;
	return true;
}

static enum cb_err media_read(void *opaque, uint64_t offset, void *buffer,
	size_t size)
{
	struct capsule_platform_media_context context;
	const struct region_device *media;

	if (!buffer || !media_context_snapshot(opaque, &context) ||
	    !range_valid(offset, size, context.media_size) ||
	    !media_current(&context, &media) ||
	    memcmp(opaque, &context, sizeof(context)) ||
	    rdev_readat(media, buffer, (size_t)offset, size) != (ssize_t)size ||
	    memcmp(opaque, &context, sizeof(context)))
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err media_erase(void *opaque, uint64_t offset, size_t size)
{
	struct capsule_platform_media_context context;
	const struct region_device *media;

	if (!media_context_snapshot(opaque, &context) ||
	    offset % context.erase_size || size % context.erase_size ||
	    !range_valid(offset, size, context.media_size) ||
	    !media_current(&context, &media) ||
	    memcmp(opaque, &context, sizeof(context)) ||
	    rdev_eraseat(media, (size_t)offset, size) != (ssize_t)size ||
	    memcmp(opaque, &context, sizeof(context)))
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err media_write(void *opaque, uint64_t offset,
	const void *buffer, size_t size)
{
	struct capsule_platform_media_context context;
	const struct region_device *media;

	if (!buffer || !media_context_snapshot(opaque, &context) ||
	    offset % context.block_size || size % context.block_size ||
	    !range_valid(offset, size, context.media_size) ||
	    !media_current(&context, &media) ||
	    memcmp(opaque, &context, sizeof(context)) ||
	    rdev_writeat(media, buffer, (size_t)offset, size) != (ssize_t)size ||
	    memcmp(opaque, &context, sizeof(context)))
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err media_sync(void *opaque)
{
	struct capsule_platform_media_context context;
	const struct region_device *media;

	/* SPI region-device operations wait for controller completion. */
	return media_context_snapshot(opaque, &context) &&
		media_current(&context, &media) &&
		!memcmp(opaque, &context, sizeof(context)) ? CB_SUCCESS : CB_ERR;
}

static bool media_source_valid(void *opaque, const void *source, size_t size)
{
	struct capsule_platform_media_context context;
	uintptr_t base;
	uintptr_t address = (uintptr_t)source;

	if (!source || !size || !media_context_snapshot(opaque, &context) ||
	    address > UINTPTR_MAX - size)
		return false;
	base = (uintptr_t)context.staging_base;
	return address >= base && size <= context.staging_size &&
		address - base <= context.staging_size - size &&
		!memcmp(opaque, &context, sizeof(context));
}

static bool facts_valid(const struct capsule_platform_facts *facts)
{
	return facts->revision == CAPSULE_PLATFORM_FACTS_REVISION &&
		facts->size == sizeof(*facts) && facts->boot_media_size &&
		facts->boot_media_size <= SIZE_MAX && facts->block_size &&
		facts->erase_size &&
		facts->boot_media_size % facts->erase_size == 0 &&
		facts->erase_size % facts->block_size == 0 &&
		facts->fmap.size == facts->boot_media_size &&
		facts->firmware.tag == LB_TAG_EFI_FW_INFO &&
		facts->firmware.size == sizeof(facts->firmware) &&
		facts->firmware.fw_size == facts->boot_media_size &&
		facts->firmware.version &&
		facts->firmware.lowest_supported_version <= facts->firmware.version &&
		bytes_present(facts->firmware.guid,
			sizeof(facts->firmware.guid)) &&
		facts->buffers.staging_base <= UINTPTR_MAX &&
		facts->buffers.staging_size <= SIZE_MAX &&
		facts->buffers.staging_size >= facts->boot_media_size;
}

enum cb_err capsule_platform_media_adapter_build(
	const struct capsule_platform_facts *facts,
	struct capsule_platform_media_context *context,
	struct capsule_media_backend *backend)
{
	struct capsule_platform_media_context candidate;
	enum cb_err status = CB_ERR;

	if (!facts || !context || !backend ||
	    ranges_overlap(facts, sizeof(*facts), context, sizeof(*context)) ||
	    ranges_overlap(facts, sizeof(*facts), backend, sizeof(*backend)) ||
	    ranges_overlap(context, sizeof(*context), backend, sizeof(*backend)) ||
	    ranges_overlap(facts, sizeof(*facts), &build_workspace,
		sizeof(build_workspace)) ||
	    ranges_overlap(context, sizeof(*context), &build_workspace,
		sizeof(build_workspace)) ||
	    ranges_overlap(backend, sizeof(*backend), &build_workspace,
		sizeof(build_workspace)) || !build_acquire())
		return CB_ERR;
	memcpy(&build_workspace.facts, facts, sizeof(build_workspace.facts));
	memset(context, 0, sizeof(*context));
	memset(backend, 0, sizeof(*backend));
	if (!facts_valid(&build_workspace.facts) ||
	    memcmp(facts, &build_workspace.facts,
		sizeof(build_workspace.facts)))
		goto out;
	candidate = (struct capsule_platform_media_context) {
		.media_size = build_workspace.facts.boot_media_size,
		.staging_base = build_workspace.facts.buffers.staging_base,
		.staging_size = build_workspace.facts.buffers.staging_size,
		.block_size = build_workspace.facts.block_size,
		.erase_size = build_workspace.facts.erase_size,
	};
	if (!media_context_valid(&candidate) ||
	    memcmp(facts, &build_workspace.facts,
		sizeof(build_workspace.facts)))
		goto out;
	*context = candidate;
	*backend = (struct capsule_media_backend) {
		.context = context,
		.size = candidate.media_size,
		.erase_size = candidate.erase_size,
		.read = media_read,
		.erase = media_erase,
		.write = media_write,
		.sync = media_sync,
		.source_valid = media_source_valid,
	};
	if (memcmp(facts, &build_workspace.facts,
		sizeof(build_workspace.facts))) {
		memset(context, 0, sizeof(*context));
		memset(backend, 0, sizeof(*backend));
		goto out;
	}
	status = CB_SUCCESS;
out:
	build_release();
	return status;
}

bool capsule_platform_smm_storage_contains(void *context,
	const void *storage, size_t size)
{
	struct region smram;
	struct region candidate;
	uintptr_t smram_base;
	size_t smram_size;

	if (context || !storage || !size ||
	    region_create_untrusted(&candidate, (uintptr_t)storage, size) !=
		CB_SUCCESS)
		return false;
	smm_region(&smram_base, &smram_size);
	if (!smram_size ||
	    region_create_untrusted(&smram, smram_base, smram_size) != CB_SUCCESS)
		return false;
	return region_is_subregion(&smram, &candidate);
}

static bool text_valid(const char *text, size_t size)
{
	if (!text || !size || size >= PAYLOAD_MM_FMP_BOARD_IDENTITY_SIZE)
		return false;
	for (size_t i = 0; i < size; i++)
		if ((uint8_t)text[i] < ' ' || (uint8_t)text[i] > '~')
			return false;
	return text[size] == '\0';
}

enum cb_err capsule_platform_identity_build(
	const struct capsule_platform_facts *facts,
	struct capsule_platform_identity *identity)
{
	struct capsule_platform_identity candidate = { 0 };
	const size_t vendor_size = strlen(mainboard_vendor);
	const size_t part_size = strlen(mainboard_part_number);
	enum cb_err status = CB_ERR;

	if (!facts || !identity ||
	    ranges_overlap(facts, sizeof(*facts), identity, sizeof(*identity)) ||
	    ranges_overlap(facts, sizeof(*facts), &build_workspace,
		sizeof(build_workspace)) ||
	    ranges_overlap(identity, sizeof(*identity), &build_workspace,
		sizeof(build_workspace)) || !build_acquire())
		return CB_ERR;
	memcpy(&build_workspace.facts, facts, sizeof(build_workspace.facts));
	memset(identity, 0, sizeof(*identity));
	if (!facts_valid(&build_workspace.facts) ||
	    !text_valid(mainboard_vendor, vendor_size) ||
	    !text_valid(mainboard_part_number, part_size) ||
	    memcmp(facts, &build_workspace.facts,
		sizeof(build_workspace.facts)))
		goto out;
	memcpy(candidate.state.namespace_guid.b,
		build_workspace.facts.firmware.guid,
		sizeof(candidate.state.namespace_guid.b));
	candidate.state.revision = PAYLOAD_MM_FMP_STATE_POLICY_REVISION;
	candidate.state.size = sizeof(candidate.state);
	candidate.state.trusted_lowest_version =
		build_workspace.facts.firmware.lowest_supported_version;
	candidate.info = (struct capsule_broker_info_policy) {
		.revision = CAPSULE_BROKER_INFO_POLICY_REVISION,
		.size = sizeof(candidate.info),
		.image_type = candidate.state.namespace_guid,
		.current_version = build_workspace.facts.firmware.version,
		.lowest_supported_version =
			build_workspace.facts.firmware.lowest_supported_version,
		.image_size = build_workspace.facts.firmware.fw_size,
		.capabilities = LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES,
	};
	candidate.authentication.image_type = candidate.state.namespace_guid;
	candidate.authentication.trusted_lowest_version =
		build_workspace.facts.firmware.lowest_supported_version;
	candidate.authentication.image_size =
		build_workspace.facts.firmware.fw_size;
	candidate.authentication.mainboard_vendor_size = (uint32_t)vendor_size;
	candidate.authentication.mainboard_part_size = (uint32_t)part_size;
	memcpy(candidate.authentication.mainboard_vendor, mainboard_vendor,
		vendor_size);
	memcpy(candidate.authentication.mainboard_part, mainboard_part_number,
		part_size);
	if (memcmp(facts, &build_workspace.facts,
		sizeof(build_workspace.facts)))
		goto out;
	*identity = candidate;
	status = CB_SUCCESS;
out:
	build_release();
	return status;
}
