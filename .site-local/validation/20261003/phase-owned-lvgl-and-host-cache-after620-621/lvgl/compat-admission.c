/* SPDX-License-Identifier: GPL-2.0-only */

/* Targeted HOST admission block; earlier hardware and later console checks
 * are not executed. The real dispatcher and transaction validator are linked.
 * Its start callback models a returned resident image, not native PE loading. */
#define main splash_fixture_main
#include CDK2_SPLASH_FIXTURE_SOURCE
#undef main

#if CONFIG_CDK2_LINEAR_SETUP_HOTKEY
static uint8_t *advertised_source;
static uint8_t owned_source[4];
static unsigned int extracted_releases;
static uint8_t owned_path[4] = {0x7fU, 0xffU, 4U, 0U};
static struct cdk2_dxe_image_transaction transaction;
static struct cdk2_dxe_pool_record allocations_owned[3];
static struct cdk2_lvgl_status_protocol ui_protocol = {
	.revision = CDK2_LVGL_STATUS_PROTOCOL_REVISION,
	.size = sizeof(ui_protocol), .show_status = record_status,
};
static unsigned int admission_mutation;
static unsigned int dispatch_calls;

static EFI_STATUS model_ui_start(const EFI_GUID *guid, const void *source,
	UINTN bytes, enum cdk2_dxe_driver_origin origin, void *opaque)
{
	struct cdk2_dxe_image *image = &core.images.images[0];
	uintptr_t code = (uintptr_t)record_status;
	uintptr_t data = (uintptr_t)&ui_protocol;
	uintptr_t base = code < data ? code : data;
	uintptr_t end = code > data + sizeof(ui_protocol) ? code + 1U : data + sizeof(ui_protocol);
	void *handle = image;

	(void)opaque;
	assert(memcmp(guid, &input_ui_drivers[0].guid, sizeof(*guid)) == 0);
	assert(source == advertised_source && bytes == sizeof(owned_source));
	assert(origin == CDK2_DXE_DRIVER_FIRMWARE_VOLUME);
	dispatch_calls++;
	memcpy(owned_source, source, bytes);
	transaction = (struct cdk2_dxe_image_transaction) {
		.source = owned_source, .source_size = sizeof(owned_source),
		.source_allocation = owned_source, .source_allocation_size = sizeof(owned_source),
		.source_generation = 12U, .device_path = owned_path,
		.device_path_size = sizeof(owned_path), .device_path_allocation = owned_path,
		.device_path_allocation_size = sizeof(owned_path), .device_path_generation = 13U,
	};
	allocations_owned[0] = (struct cdk2_dxe_pool_record) {
		.base = &transaction, .size = sizeof(transaction), .generation = 11U, .live = TRUE,
	};
	allocations_owned[1] = (struct cdk2_dxe_pool_record) {
		.base = owned_source, .size = sizeof(owned_source), .generation = 12U, .live = TRUE,
	};
	allocations_owned[2] = (struct cdk2_dxe_pool_record) {
		.base = owned_path, .size = sizeof(owned_path), .generation = 13U, .live = TRUE,
	};
	core.memory.pools = allocations_owned;
	core.memory.pool_count = ARRAY_SIZE(allocations_owned);
	core.images.memory = &core.memory;
	*image = (struct cdk2_dxe_image) {
		.handle = image, .base = (void *)base, .size = end - base,
		.load_generation = core.images.next_handle++, .started = TRUE, .exited = TRUE,
		.exit_status = EFI_SUCCESS, .protocol_pins = 1U,
		.transaction = &transaction, .transaction_allocation_generation = 11U,
	};
	core.images.count = 1U;
	if (admission_mutation == 1U)
		owned_source[0] ^= 1U;
	if (admission_mutation == 2U)
		image->load_generation++;
	if (admission_mutation == 3U)
		image->transaction_allocation_generation++;
	/* Model the real LoadImage handle registration, not an installation on
	 * an unknown handle (which the actual database correctly refuses). */
	core.database.handles[core.database.handle_count++] = handle;
	assert(cdk2_dxe_install(&core.database, &handle, &lvgl_status_protocol_guid,
		&ui_protocol) == EFI_SUCCESS);
	core.database.protocols[0].owner_image = image;
	core.database.protocols[0].owner_generation = image->load_generation;
	return EFI_SUCCESS;
}

static EFI_STATUS execute_admission(struct dxe_services_context *context)
{
	struct cdk2_dxe_protocol *status_record;
	struct cdk2_dxe_image *status_owner;
	uint64_t ui_generation;
	struct linear_status_output status_output = {0};
	EFI_STATUS status;
#if CDK2_STRICT_DIRECT_RUNTIME
	const void *ui_source = NULL;
	size_t ui_source_size = 0U;
#endif

#include CDK2_UI_ADMISSION_BLOCK
	/* Model the unchanged later phase checks as successful, then publish the
	 * exact candidate produced by the real admission block. */
	context->status_output = status_output;
	return status;
}

static void release_source(void *source, void *opaque)
{
	(void)opaque;
	assert(source == advertised_source);
	free(source);
	advertised_source = NULL;
	extracted_releases++;
}

int main(void)
{
	for (admission_mutation = 0U; admission_mutation < 4U; admission_mutation++) {
		struct dxe_services_context context = {0};
		EFI_STATUS status;

		memset(&core, 0, sizeof(core));
		dispatch_calls = 0U;
		assert(cdk2_dxe_database_initialize(&core.database) == EFI_SUCCESS);
		assert(cdk2_dxe_dispatcher_initialize(&core.dispatcher,
			protocol_present, model_ui_start, NULL) == EFI_SUCCESS);
		core.images.next_handle = 7U;
		advertised_source = malloc(sizeof(owned_source));
		assert(advertised_source != NULL);
		memcpy(advertised_source, "ABCD", sizeof(owned_source));
		extracted_releases = 0U;
		core.dispatcher.release = release_source;
		core.dispatcher.count = 1U;
		core.dispatcher.drivers[0] = (struct cdk2_dxe_driver) {
			.guid = input_ui_drivers[0].guid, .image = advertised_source,
			.image_size = sizeof(owned_source), .direct = FALSE,
			.extracted = {advertised_source}, .extracted_count = 1U,
		};
		status = execute_admission(&context);
		assert(dispatch_calls == 1U);
		assert(extracted_releases == 1U && advertised_source == NULL);
		assert(core.dispatcher.drivers[0].image == NULL);
		if (admission_mutation <= 1U) {
			assert(status == EFI_SUCCESS && context.status_output.generation == 7U);
			assert(context.status_output.image == &core.images.images[0]);
			assert(context.status_output.interface == &ui_protocol);
			assert(context.status_output.show_status == record_status);
		} else {
			if (admission_mutation == 2U)
				assert(status == EFI_COMPROMISED_DATA && "admission first-load generation refusal");
			if (admission_mutation == 3U)
				assert(status == EFI_COMPROMISED_DATA && "admission owned transaction refusal");
			assert(context.status_output.image == NULL && context.status_output.interface == NULL);
		}
	}
	puts("Compatibility HOST admission: freed extraction source, owned transaction and generation PASS");
	return 0;
}
#else
#error "The compatibility proof requires a real resolved hotkey-enabled configuration"
#endif
