#if CONFIG_CDK2_LINEAR_SETUP_HOTKEY
	ui_generation = core.images.next_handle;
#if CDK2_STRICT_DIRECT_RUNTIME
	if (core.dispatcher.count > CDK2_DXE_MAX_DRIVERS)
		return EFI_COMPROMISED_DATA;
	for (size_t index = 0U; index < core.dispatcher.count; index++) {
		const struct cdk2_dxe_driver *driver = &core.dispatcher.drivers[index];

		if (memcmp(&driver->guid, &input_ui_drivers[0].guid, sizeof(driver->guid)) != 0)
			continue;
		if (ui_source != NULL || !driver->direct || driver->started || driver->failed ||
		    driver->image == NULL || driver->image_size == 0U)
			return EFI_COMPROMISED_DATA;
		ui_source = driver->image;
		ui_source_size = driver->image_size;
	}
	if (ui_source == NULL)
		return EFI_NOT_FOUND;
#endif
	if (context->status_output.image != NULL || ui_generation == 0U ||
	    ui_generation == MAX_UINTN)
		return EFI_COMPROMISED_DATA;
	status = dispatch_required(context, input_ui_drivers,
		ARRAY_SIZE(input_ui_drivers));
	if (EFI_ERROR(status))
		return status;
	status = private_control_record(&core, &lvgl_status_protocol_guid,
		sizeof(*status_output.interface), &status_record, &status_owner);
	if (status != EFI_SUCCESS || status_owner == NULL ||
	    status_owner->load_generation != ui_generation ||
	    status_record->handle != status_owner->handle ||
	    EFI_ERROR(cdk2_dxe_image_transaction_validate_owned(&core.images,
		status_owner->transaction, status_owner->transaction_allocation_generation)))
		return EFI_COMPROMISED_DATA;
#if CDK2_STRICT_DIRECT_RUNTIME
	if (status_owner->transaction->source_size != ui_source_size ||
	    memcmp(status_owner->transaction->source, ui_source, ui_source_size) != 0)
		return EFI_COMPROMISED_DATA;
#endif
	status_output.image = status_owner->handle;
	status_output.generation = status_owner->load_generation;
	status_output.interface = status_record->interface;
	status_output.show_status = status_output.interface->show_status;
	status = validate_linear_status_output(&status_output);
	if (EFI_ERROR(status))
		return status;
#endif
