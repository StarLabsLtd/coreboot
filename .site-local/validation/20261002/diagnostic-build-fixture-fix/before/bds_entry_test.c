/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cdk2/config.h>
#include <cdk2/capsule_boot.h>
#include <cdk2/capsule_runtime.h>

#undef CONFIG_CDK2_SETUP_UI
#undef CONFIG_CDK2_CAPSULE
#undef CONFIG_CDK2_BOOT_TIMEOUT
#undef CONFIG_CDK2_NATIVE_LVGL_SETUP
#undef CONFIG_CDK2_SECURE_BOOT_CONFIG
#undef CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT
#define CONFIG_CDK2_SETUP_UI 1
#define CONFIG_CDK2_CAPSULE 1
#define CONFIG_CDK2_BOOT_TIMEOUT 1
#ifdef CDK2_BDS_TEST_CAPSULE_DISK
#define CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT 1
#else
#define CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT 0
#endif
#ifdef CDK2_BDS_TEST_SETUP_DISABLED
#define CONFIG_CDK2_NATIVE_LVGL_SETUP 0
#else
#define CONFIG_CDK2_NATIVE_LVGL_SETUP 1
#endif
#define CONFIG_CDK2_SECURE_BOOT_CONFIG 0
#define CDK2_BDS_UNIT_TEST
#define cdk2_bds_browse_settings test_bds_browse_settings
#include "../src/modules/bds/entry.c"
#undef cdk2_bds_browse_settings

static const EFI_GUID capsule_vendor_guid = { 0x711c703f, 0xc285, 0x4b10,
	{ 0xa3, 0xb0, 0x36, 0xec, 0xbd, 0x3c, 0x8b, 0xe2 } };
static const EFI_GUID capsule_boot_guid = { 0x2890d42b, 0x7f9b, 0x4a66,
	{ 0x9a, 0x12, 0x54, 0x8d, 0x6b, 0x57, 0xd8, 0x20 } };

static UINTN battery_reads;
static UINTN battery_refreshes, battery_poll_starts, battery_poll_stops;
static EFI_STATUS poll_stop_status = EFI_SUCCESS;
static BOOLEAN expose_battery = TRUE;
static EFI_STATUS poll_begin_status = EFI_SUCCESS;

static struct bds_protocol *installed;
static UINTN connected, loaded, started, deleted, watchdogs, boot_current;
static UINTN ready_created, ready_signaled, ready_closed;
static UINTN stalls, key_reads;
static UINT16 key_scan_code = SCAN_F2;
static UINTN queued_key_reads;
static EFI_STATUS key_read_error = EFI_SUCCESS;
static BOOLEAN scheduled_key = TRUE;
static UINTN driver_loads;
static UINTN capsules, capsule_deletes;
struct capsule_fixture_variable {
	const CHAR16 *name;
	EFI_GUID guid;
	UINT64 scatter;
	EFI_STATUS delete_status;
	BOOLEAN present, retain;
};
#define CAPSULE_FIXTURE_MAX 12U
static struct capsule_fixture_variable capsule_fixture[CAPSULE_FIXTURE_MAX];
static UINTN capsule_fixture_count, capsule_fixture_enum_calls;
static UINTN capsule_fixture_enum_fail_after = MAX_UINTN;
static UINT64 capsule_processed[CAPSULE_FIXTURE_MAX];
static UINTN capsule_processed_count;
static BOOLEAN capsule_fixture_enabled;
static struct loaded_image_protocol loaded_image;
static UINT8 partition_path[50];
static BOOLEAN expose_ui;
static BOOLEAN disable_ui_form, disable_ui_menu;
static UINTN ui_locates, fail_ui_locate_at, remove_ui_form_at;
static UINTN remove_ui_menu_at, remove_runtime_get_at, remove_runtime_set_at;
static struct runtime_services *runtime_to_drift;
static BOOLEAN expose_boot_order;
static UINTN menu_calls;
static UINTN menu_item_count;
static UINTN menu_queued_keys;
static BOOLEAN menu_select_next_once;
static BOOLEAN menu_select_setup_once;
static BOOLEAN menu_cancel_once;
static EFI_STATUS menu_status;
static UINTN locate_all_calls;
static UINTN native_browse_calls;
static EFI_STATUS native_browse_status;
static BOOLEAN os_indications_present;
static UINT64 os_indications;
static UINT32 os_indications_attributes = OS_INDICATIONS_ATTRIBUTES;
static EFI_STATUS os_indications_set_status = EFI_SUCCESS;
static UINTN os_indications_sets;
static BOOLEAN require_fw_ui_clear_before_start;
static UINTN start_with_fw_ui_pending;
static BOOLEAN jump_on_start;
static jmp_buf start_jump;
static void *start_allocations[16];
static UINTN start_allocation_count;
static UINTN outstanding_allocations;
static BOOLEAN os_support_present;
static UINT64 os_support_value;
static UINT32 os_support_attributes = OS_INDICATIONS_SUPPORTED_ATTRIBUTES;
static EFI_STATUS os_support_set_status = EFI_SUCCESS;
static UINTN os_support_sets;
typedef CHAR16 * char16_ptr;
typedef const EFI_GUID * guid_ptr;
typedef void *void_ptr;
typedef void **void_ptr_ptr;
typedef UINTN *uintn_ptr;
static UINT8 load_option[] = {
	1, 0, 0, 0, 4, 0, 'T', 0, 0, 0,
	END_DEVICE_PATH, END_ENTIRE, 4, 0, 0xaa, 0xbb
};
static EFI_STATUS CDK2_MS_ABI fake_show(struct cdk2_lvgl_ui_protocol *,
	const CHAR16 *, const struct cdk2_lvgl_menu_item *, UINTN, UINTN *);
static EFI_STATUS CDK2_MS_ABI fake_form(struct cdk2_lvgl_ui_protocol *,
	const struct cdk2_lvgl_form *, UINT16 *, INTN *);
static EFI_STATUS CDK2_MS_ABI fake_set_poll(struct cdk2_lvgl_ui_protocol *,
	cdk2_lvgl_poll_fn *, void *, UINTN);

EFI_STATUS test_bds_browse_settings(struct cdk2_lvgl_ui_protocol *ui,
	struct cdk2_runtime_services_view *runtime)
{
	if (ui == NULL || runtime == NULL || runtime->get_variable == NULL ||
	    runtime->set_variable == NULL)
		return EFI_INVALID_PARAMETER;
	native_browse_calls++;
	return native_browse_status;
}

static int name_is(const CHAR16 *name, const char *ascii)
{
	UINTN index;

	for (index = 0; ascii[index] != 0; index++)
		if (name[index] != (CHAR16)ascii[index])
			return 0;
	return name[index] == 0;
}

static BOOLEAN wide_name_equal(const CHAR16 *left, const CHAR16 *right)
{
	UINTN index = 0U;

	while (left[index] == right[index]) {
		if (left[index] == 0U)
			return TRUE;
		index++;
	}
	return FALSE;
}

static UINTN wide_name_size(const CHAR16 *name)
{
	UINTN characters = 1U;

	while (*name++ != 0U)
		characters++;
	return characters * sizeof(CHAR16);
}

static EFI_STATUS CDK2_MS_ABI fake_get(char16_ptr name, guid_ptr guid,
	UINT32 *attributes, uintn_ptr size, void_ptr data)
{
	const void *value;
	UINTN value_size;

	(void)attributes;
	if (capsule_fixture_enabled) {
		for (UINTN index = 0U; index < capsule_fixture_count; index++) {
			struct capsule_fixture_variable *variable = &capsule_fixture[index];

			if (!wide_name_equal(name, variable->name) ||
			    memcmp(guid, &variable->guid, sizeof(*guid)) != 0)
				continue;
			if (!variable->present)
				return EFI_NOT_FOUND;
			if (data == NULL) {
				*size = sizeof(variable->scatter);
				return EFI_BUFFER_TOO_SMALL;
			}
			if (*size < sizeof(variable->scatter))
				return EFI_BUFFER_TOO_SMALL;
			memcpy(data, &variable->scatter, sizeof(variable->scatter));
			*size = sizeof(variable->scatter);
			return EFI_SUCCESS;
		}
		return EFI_NOT_FOUND;
	}
	(void)guid;
	if (name_is(name, "CapsuleUpdateData")) {
		static const UINT64 scatter = 0x12345678U;

		if (capsule_deletes != 0U)
			return EFI_NOT_FOUND;
		value = &scatter;
		value_size = sizeof(scatter);
	} else if (name_is(name, "BootNext")) {
		static const UINT16 next = 1;

		value = &next;
		value_size = sizeof(next);
	} else if (name_is(name, "Boot0001")) {
		value = load_option;
		value_size = sizeof(load_option);
	} else if (name_is(name, "DriverOrder")) {
		static const UINT16 order = 2U;

		value = &order;
		value_size = sizeof(order);
	} else if (name_is(name, "Driver0002")) {
		value = load_option;
		value_size = sizeof(load_option);
	} else if (name_is(name, "BootOrder") && expose_boot_order) {
		static const UINT16 order = 1U;

		value = &order;
		value_size = sizeof(order);
	} else if (name_is(name, "OsIndicationsSupported") && os_support_present) {
		value = &os_support_value;
		value_size = sizeof(os_support_value);
		if (attributes != NULL)
			*attributes = os_support_attributes;
	} else if (name_is(name, "OsIndications") && os_indications_present) {
		value = &os_indications;
		value_size = sizeof(os_indications);
		if (attributes != NULL)
			*attributes = os_indications_attributes;
	} else {
		return EFI_NOT_FOUND;
	}
	if (data == NULL) {
		*size = value_size;
		return EFI_BUFFER_TOO_SMALL;
	}
	if (*size < value_size)
		return EFI_BUFFER_TOO_SMALL;
	memcpy(data, value, value_size);
	*size = value_size;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_get_next(UINTN *size, CHAR16 *name,
	EFI_GUID *guid)
{
	static const CHAR16 capsule_name[] = L"CapsuleUpdateData";
	UINTN start = 0U;

	if (capsule_fixture_enabled) {
		if (capsule_fixture_enum_calls++ == capsule_fixture_enum_fail_after)
			return EFI_DEVICE_ERROR;
		if (name[0] != 0U) {
			for (; start < capsule_fixture_count; start++)
				if (wide_name_equal(name, capsule_fixture[start].name) &&
				    memcmp(guid, &capsule_fixture[start].guid,
					sizeof(*guid)) == 0) {
					start++;
					break;
				}
			if (start > capsule_fixture_count)
				return EFI_INVALID_PARAMETER;
		}
		while (start < capsule_fixture_count && !capsule_fixture[start].present)
			start++;
		if (start == capsule_fixture_count)
			return EFI_NOT_FOUND;
		{
			UINTN required = wide_name_size(capsule_fixture[start].name);

			if (*size < required) {
				*size = required;
				return EFI_BUFFER_TOO_SMALL;
			}
			memcpy(name, capsule_fixture[start].name, required);
			*guid = capsule_fixture[start].guid;
			*size = required;
			return EFI_SUCCESS;
		}
	}

	if (name[0] != 0U)
		return EFI_NOT_FOUND;
	if (*size < sizeof(capsule_name)) {
		*size = sizeof(capsule_name);
		return EFI_BUFFER_TOO_SMALL;
	}
	memcpy(name, capsule_name, sizeof(capsule_name));
	*guid = capsule_vendor_guid;
	*size = sizeof(capsule_name);
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_set(char16_ptr name, guid_ptr guid,
	UINT32 attributes, UINTN size, void_ptr data)
{
	if (capsule_fixture_enabled) {
		for (UINTN index = 0U; index < capsule_fixture_count; index++) {
			struct capsule_fixture_variable *variable = &capsule_fixture[index];

			if (!wide_name_equal(name, variable->name) ||
			    memcmp(guid, &variable->guid, sizeof(*guid)) != 0)
				continue;
			if (attributes != 0U || size != 0U || data != NULL)
				return EFI_INVALID_PARAMETER;
			capsule_deletes++;
			if (EFI_ERROR(variable->delete_status))
				return variable->delete_status;
			if (!variable->retain)
				variable->present = FALSE;
			return EFI_SUCCESS;
		}
		return EFI_NOT_FOUND;
	}
	(void)guid;
	if (name_is(name, "OsIndicationsSupported") &&
	    attributes == OS_INDICATIONS_SUPPORTED_ATTRIBUTES &&
	    size == sizeof(UINT64) && data != NULL) {
		if (EFI_ERROR(os_support_set_status))
			return os_support_set_status;
		os_support_present = TRUE;
		os_support_value = *(UINT64 *)data;
		os_support_sets++;
		return EFI_SUCCESS;
	}
	if (name_is(name, "OsIndications") &&
	    attributes == os_indications_attributes && size == sizeof(UINT64) &&
	    data != NULL) {
		if (EFI_ERROR(os_indications_set_status))
			return os_indications_set_status;
		os_indications = *(UINT64 *)data;
		os_indications_sets++;
		return EFI_SUCCESS;
	}
	if (name_is(name, "CapsuleUpdateData") && attributes == 0U &&
	    size == 0U && data == NULL) {
		capsule_deletes++;
		return EFI_SUCCESS;
	}
	if (name_is(name, "BootCurrent")) {
		if (attributes == 6U && size == sizeof(UINT16) && data != NULL)
			boot_current++;
		else if (attributes == 0 && size == 0 && data == NULL)
			boot_current++;
		else
			return EFI_INVALID_PARAMETER;
		return EFI_SUCCESS;
	}
	if (!name_is(name, "BootNext") || attributes != 0 || size != 0 || data != NULL)
		return EFI_INVALID_PARAMETER;
	deleted++;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_allocate(UINTN type, UINTN size, void **data)
{
	(void)type;
	*data = malloc(size);
	if (*data != NULL)
		outstanding_allocations++;
	if (*data != NULL && jump_on_start &&
	    start_allocation_count < ARRAY_SIZE(start_allocations))
		start_allocations[start_allocation_count++] = *data;
	return *data == NULL ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_free(void *data)
{
	UINTN index;

	for (index = 0U; index < start_allocation_count; index++)
		if (start_allocations[index] == data)
			start_allocations[index] = NULL;
	free(data);
	if (data != NULL)
		outstanding_allocations--;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_handle(void_ptr handle, guid_ptr guid,
	void_ptr_ptr interface)
{
	(void)handle;
	if (memcmp(guid, &loaded_image_guid, sizeof(*guid)) == 0) {
		*interface = &loaded_image;
		return EFI_SUCCESS;
	}
	if (handle == (void *)0x5000 &&
	    memcmp(guid, &path_guid, sizeof(*guid)) == 0) {
		*interface = partition_path;
		return EFI_SUCCESS;
	}
	return EFI_NOT_FOUND;
}

static EFI_STATUS CDK2_MS_ABI fake_load(BOOLEAN boot_policy, void *parent,
	void *path, void *source, UINTN size, void **handle)
{
	const UINT8 *bytes = path;

	loaded_image.load_options = NULL;
	loaded_image.load_options_size = 0U;

	(void)parent;
	(void)source;
	(void)size;
	if (bytes == NULL ||
	    !((bytes[0] == END_DEVICE_PATH && bytes[1] == END_ENTIRE) ||
	      (bytes[0] == 1U && bytes[1] == 1U && bytes[4] == MEDIA_DEVICE_PATH &&
	       (bytes[5] == HARD_DRIVE_SUBTYPE || bytes[5] == FV_FILE_SUBTYPE))))
		return EFI_INVALID_PARAMETER;
	if (!boot_policy)
		driver_loads++;
	loaded++;
	*handle = (void *)0x3000;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_start(void *handle, UINTN *size,
	void *output)
{
	CHAR16 **data = output;

	(void)handle;
	if (require_fw_ui_clear_before_start &&
	    (os_indications & OS_INDICATIONS_BOOT_TO_FW_UI) != 0U)
		start_with_fw_ui_pending++;
	if (jump_on_start)
		longjmp(start_jump, 1);
	if (loaded_image.load_options_size != 0U &&
	    (loaded_image.load_options_size != 2U ||
	    memcmp(loaded_image.load_options, "\xaa\xbb", 2U) != 0))
		return EFI_INVALID_PARAMETER;
	*size = 0;
	*data = NULL;
	started++;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_unload(void *handle)
{ (void)handle; return EFI_SUCCESS; }

static EFI_STATUS CDK2_MS_ABI fake_create_ex(UINT32 type, UINTN tpl,
	void *notify, const void *context, const EFI_GUID *group, void **event)
{
	(void)context;
	if (type != 0 || tpl != 0 || notify != NULL ||
	    (memcmp(group, &ready_to_boot_guid, sizeof(*group)) != 0 &&
	    memcmp(group, &end_of_dxe_guid, sizeof(*group)) != 0))
		return EFI_INVALID_PARAMETER;
	ready_created++;
	*event = (void *)0x4000;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_create_event(UINT32 type, UINTN tpl,
	event_notify_fn *notify, void *context, void **event)
{
	(void)type;
	(void)tpl;
	(void)notify;
	(void)context;
	*event = (void *)0x5000;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_set_timer(void *event, UINTN type,
	UINT64 trigger)
{
	if (event != (void *)0x5000)
		return EFI_INVALID_PARAMETER;
	(void)type;
	(void)trigger;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_signal(void *event)
{
	if (event != (void *)0x4000)
		return EFI_INVALID_PARAMETER;
	ready_signaled++;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_close(void *event)
{
	if (event == (void *)0x5000)
		return EFI_SUCCESS;
	if (event != (void *)0x4000)
		return EFI_INVALID_PARAMETER;
	ready_closed++;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_watchdog(UINTN timeout, UINT64 code,
	UINTN size, CHAR16 *data)
{
	if (timeout != 0 || code != 0 || size != 0 || data != NULL)
		return EFI_INVALID_PARAMETER;
	watchdogs++;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_stall(UINTN microseconds)
{
	if (microseconds != 100000U)
		return EFI_INVALID_PARAMETER;
	stalls++;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_read_key(void *input,
	struct input_key *key)
{
	(void)input;
	key_reads++;
	if (EFI_ERROR(key_read_error))
		return key_read_error;
	key->scan_code = key_scan_code;
	key->unicode_char = 0U;
	if (queued_key_reads != 0U) {
		queued_key_reads--;
		return EFI_SUCCESS;
	}
	return scheduled_key && key_reads == 2U ? EFI_SUCCESS : EFI_NOT_READY;
}

static EFI_STATUS CDK2_MS_ABI fake_connect(void *handle, void *drivers,
	void *path, BOOLEAN recursive)
{
	(void)handle;
	(void)drivers;
	(void)path;
	if (!recursive)
		return EFI_INVALID_PARAMETER;
	connected++;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_locate(UINTN search, guid_ptr guid,
	void *key, UINTN *count, void ***handles)
{
	(void)guid;
	(void)key;
	if (search == LOCATE_BY_PROTOCOL && guid != NULL &&
	    memcmp(guid, &path_guid, sizeof(*guid)) == 0) {
		*handles = malloc(sizeof(**handles));
		if (*handles == NULL)
			return EFI_OUT_OF_RESOURCES;
		outstanding_allocations++;
		(*handles)[0] = (void *)0x5000;
		*count = 1;
		return EFI_SUCCESS;
	}
	if (search == LOCATE_BY_PROTOCOL && guid != NULL &&
	    memcmp(guid, &fv2_guid, sizeof(*guid)) == 0) {
		*handles = malloc(sizeof(**handles));
		if (*handles == NULL)
			return EFI_OUT_OF_RESOURCES;
		outstanding_allocations++;
		(*handles)[0] = (void *)0x5000;
		*count = 1;
		return EFI_SUCCESS;
	}
	if (search == LOCATE_BY_PROTOCOL && guid != NULL &&
	    memcmp(guid, &simple_fs_guid, sizeof(*guid)) == 0) {
		*handles = malloc(sizeof(**handles));
		if (*handles == NULL)
			return EFI_OUT_OF_RESOURCES;
		outstanding_allocations++;
		(*handles)[0] = (void *)0x5000;
		*count = 1;
		return EFI_SUCCESS;
	}
	if (search != LOCATE_ALL_HANDLES)
		return EFI_NOT_FOUND;
	locate_all_calls++;
	*handles = malloc(sizeof(**handles));
	if (*handles == NULL)
		return EFI_OUT_OF_RESOURCES;
	outstanding_allocations++;
	(*handles)[0] = (void *)0x2000;
	*count = 1;
	return EFI_SUCCESS;
}

static struct cdk2_capsule_boot_protocol capsule_protocol;
static EFI_STATUS memory_lookup_status = EFI_NOT_FOUND;
static EFI_STATUS memory_initialize_status, memory_perform_status;
static EFI_STATUS memory_finish_status;
static uint8_t memory_reported_error;
static struct cdk2_generic_memory_test *memory_interface;
static UINTN memory_initialize_calls, memory_perform_calls, memory_finish_calls;

static uint64_t CDK2_MS_ABI fake_memory_initialize(
	struct cdk2_generic_memory_test *test, uint32_t level, uint8_t *soft_ecc)
{
	(void)test;
	(void)level;
	*soft_ecc = 0;
	memory_initialize_calls++;
	return memory_initialize_status;
}

static uint64_t CDK2_MS_ABI fake_memory_perform(
	struct cdk2_generic_memory_test *test, uint64_t *tested, uint64_t *total,
	uint8_t *error, uint8_t abort)
{
	(void)test;
	(void)abort;
	*tested = *total = 0;
	*error = memory_reported_error;
	memory_perform_calls++;
	return memory_perform_status;
}

static uint64_t CDK2_MS_ABI fake_memory_finished(
	struct cdk2_generic_memory_test *test)
{
	(void)test;
	memory_finish_calls++;
	return memory_finish_status;
}

static EFI_STATUS CDK2_MS_ABI fake_battery_critical(BOOLEAN *critical)
{
	battery_reads++;
	*critical = TRUE;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_battery_refresh(void)
{
	battery_refreshes++;
	return EFI_SUCCESS;
}

static struct cdk2_battery_protocol battery_protocol = {
	.get_critical = fake_battery_critical,
	.refresh = fake_battery_refresh
};

static EFI_STATUS CDK2_MS_ABI fake_process_capsule(
	struct cdk2_capsule_boot_protocol *self, UINT64 scatter)
{
	if (self != &capsule_protocol)
		return EFI_INVALID_PARAMETER;
	if (capsule_fixture_enabled) {
		if (capsule_processed_count >= ARRAY_SIZE(capsule_processed))
			return EFI_OUT_OF_RESOURCES;
		capsule_processed[capsule_processed_count++] = scatter;
		capsules++;
		return EFI_SUCCESS;
	}
	if (scatter != 0x12345678U)
		return EFI_INVALID_PARAMETER;
	capsules++;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_locate_protocol(guid_ptr guid, void *key,
	void **interface)
{
	static struct cdk2_lvgl_ui_protocol ui;

	(void)key;
	if (memcmp(guid, &memory_test_guid, sizeof(*guid)) == 0) {
		*interface = memory_interface;
		return memory_lookup_status;
	}
	if (memcmp(guid, &capsule_boot_guid, sizeof(*guid)) == 0) {
		capsule_protocol.process_scatter = fake_process_capsule;
		*interface = &capsule_protocol;
	} else if (memcmp(guid, &cdk2_battery_protocol_guid,
	    sizeof(*guid)) == 0) {
		if (!expose_battery)
			return EFI_NOT_FOUND;
		*interface = &battery_protocol;
	} else if (expose_ui && memcmp(guid, &lvgl_ui_guid, sizeof(*guid)) == 0) {
		ui_locates++;
		if (ui_locates == fail_ui_locate_at)
			return EFI_NOT_FOUND;
		ui.show_menu = disable_ui_menu ? NULL : fake_show;
		ui.show_form = disable_ui_form ? NULL : fake_form;
		if (ui_locates == remove_ui_menu_at)
			ui.show_menu = NULL;
		if (ui_locates == remove_ui_form_at)
			ui.show_form = NULL;
		if (runtime_to_drift != NULL && ui_locates == remove_runtime_get_at)
			runtime_to_drift->get_variable = NULL;
		if (runtime_to_drift != NULL && ui_locates == remove_runtime_set_at)
			runtime_to_drift->set_variable = NULL;
		ui.set_poll = fake_set_poll;
		*interface = &ui;
	} else {
		return EFI_NOT_FOUND;
	}
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_set_poll(struct cdk2_lvgl_ui_protocol *self,
	cdk2_lvgl_poll_fn *poll, void *context, UINTN period_ms)
{
	(void)self;
	if (poll != NULL && period_ms == 10U) {
		battery_poll_starts++;
		if (EFI_ERROR(poll_begin_status))
			return poll_begin_status;
		for (UINTN tick = 0U; tick < 100U; tick++)
			poll(context);
		return EFI_SUCCESS;
	}
	if (poll == NULL && context == NULL && period_ms == 0U) {
		battery_poll_stops++;
		return poll_stop_status;
	}
	return EFI_INVALID_PARAMETER;
}

static EFI_STATUS CDK2_MS_ABI fake_show(struct cdk2_lvgl_ui_protocol *self,
	const CHAR16 *title, const struct cdk2_lvgl_menu_item *items,
	UINTN count, UINTN *selection)
{
	(void)self;
	if (title == NULL || items == NULL || count == 0U || items[0].label == NULL)
		return EFI_INVALID_PARAMETER;
	menu_calls++;
	menu_item_count = count;
	menu_queued_keys = queued_key_reads;
	if (EFI_ERROR(menu_status))
		return menu_status;
	if (menu_cancel_once) {
		menu_cancel_once = FALSE;
		*selection = MAX_UINTN;
	} else if (menu_select_setup_once) {
		menu_select_setup_once = FALSE;
		*selection = count - 1U;
	} else if (menu_select_next_once && count == CDK2_LVGL_MENU_MAX_ITEMS) {
		menu_select_next_once = FALSE;
		*selection = count - 2U;
	} else {
		*selection = 0;
	}
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fake_form(struct cdk2_lvgl_ui_protocol *self,
	const struct cdk2_lvgl_form *form, UINT16 *question, INTN *adjustment)
{
	(void)self; (void)form; (void)question; (void)adjustment;
	return EFI_ABORTED;
}

static EFI_STATUS CDK2_MS_ABI fake_install(void **handle, ...)
{
	(void)handle;
	installed = &protocol;
	return EFI_SUCCESS;
}

static int expect(int condition, const char *message)
{
	if (!condition)
		fprintf(stderr, "FAIL: %s\n", message);
	return !condition;
}

int main(void)
{
	UINT8 short_path[46] = { MEDIA_DEVICE_PATH, HARD_DRIVE_SUBTYPE, 42, 0 };
	UINT8 stale_full_path[50] = { 2U, 1U, 4U, 0U };
	struct cdk2_bds_option short_option;
	struct cdk2_bds_option stale_full_option;
	struct runtime_services runtime = { 0 };
	struct boot_services boot = { 0 };
	struct system_table table = { 0 };
	struct bds_hob_list {
		EFI_HOB_HANDOFF_INFO_TABLE handoff;
		EFI_HOB_GENERIC_HEADER end;
	} hob_list = {
		.handoff = {
			.header = { EFI_HOB_TYPE_HANDOFF,
				sizeof(EFI_HOB_HANDOFF_INFO_TABLE), 0U },
			.version = EFI_HOB_HANDOFF_TABLE_VERSION,
			.boot_mode = BOOT_ON_S4_RESUME,
		},
		.end = { EFI_HOB_TYPE_END_OF_HOB_LIST,
			sizeof(EFI_HOB_GENERIC_HEADER), 0U },
	};
	struct configuration_table configuration = {
		.guid = hob_list_guid,
		.table = &hob_list.handoff,
	};
	struct simple_text_input input = { .read_key = fake_read_key };
	EFI_STATUS boot_status;
	volatile int failures = 0;
	struct cdk2_generic_memory_test memory_protocol = {
		.initialize = fake_memory_initialize,
		.perform = fake_memory_perform,
		.finished = fake_memory_finished,
	};

	runtime.get_variable = fake_get;
	runtime.get_next_variable_name = fake_get_next;
	runtime.set_variable = fake_set;
	boot.allocate_pool = fake_allocate;
	boot.free_pool = fake_free;
	boot.create_event = fake_create_event;
	boot.set_timer = fake_set_timer;
	boot.signal_event = fake_signal;
	boot.close_event = fake_close;
	boot.handle_protocol = fake_handle;
	boot.load_image = fake_load;
	boot.start_image = fake_start;
	boot.unload_image = fake_unload;
	boot.set_watchdog_timer = fake_watchdog;
	boot.stall = fake_stall;
	boot.connect_controller = fake_connect;
	boot.locate_handle_buffer = fake_locate;
	boot.locate_protocol = fake_locate_protocol;
	boot.install_multiple = fake_install;
	boot.create_event_ex = fake_create_ex;
	table.runtime = &runtime;
	table.boot = &boot;
	table.table_count = 1U;
	table.tables = &configuration;
	hob_list.handoff.efi_memory_bottom = (UINTN)&hob_list;
	hob_list.handoff.efi_memory_top = (UINTN)&hob_list + sizeof(hob_list);
	hob_list.handoff.efi_free_memory_bottom = (UINTN)&hob_list +
		sizeof(hob_list);
	hob_list.handoff.efi_free_memory_top = (UINTN)&hob_list + sizeof(hob_list);
	hob_list.handoff.efi_end_of_hob_list = (UINTN)&hob_list.end;
	active_system = &table;
	failures += complete_memory_test() != EFI_SUCCESS;
	memory_lookup_status = EFI_DEVICE_ERROR;
	failures += complete_memory_test() != EFI_UNSUPPORTED;
	memory_lookup_status = EFI_SUCCESS;
	failures += complete_memory_test() != EFI_UNSUPPORTED;
	memory_interface = &memory_protocol;
	memory_protocol.initialize = NULL;
	failures += complete_memory_test() != EFI_UNSUPPORTED;
	memory_protocol.initialize = fake_memory_initialize;
	memory_protocol.perform = NULL;
	failures += complete_memory_test() != EFI_UNSUPPORTED;
	memory_protocol.perform = fake_memory_perform;
	memory_protocol.finished = NULL;
	failures += complete_memory_test() != EFI_UNSUPPORTED;
	memory_protocol.finished = fake_memory_finished;
	memory_initialize_status = EFI_DEVICE_ERROR;
	failures += complete_memory_test() != EFI_DEVICE_ERROR;
	failures += memory_initialize_calls != 1 || memory_perform_calls != 0 ||
		memory_finish_calls != 0;
	memory_initialize_status = EFI_SUCCESS;
	memory_perform_status = EFI_DEVICE_ERROR;
	failures += complete_memory_test() != EFI_DEVICE_ERROR;
	failures += memory_perform_calls != 1 || memory_finish_calls != 0;
	memory_perform_status = EFI_NOT_FOUND;
	memory_reported_error = 1;
	failures += complete_memory_test() != EFI_DEVICE_ERROR;
	failures += memory_finish_calls != 0;
	memory_reported_error = 0;
	memory_finish_status = EFI_DEVICE_ERROR;
	failures += complete_memory_test() != EFI_DEVICE_ERROR;
	failures += memory_finish_calls != 1;
	memory_finish_status = EFI_SUCCESS;
	failures += complete_memory_test() != EFI_SUCCESS;
	failures += memory_finish_calls != 2;
	memory_lookup_status = EFI_NOT_FOUND;
	memory_interface = NULL;
	runtime_to_drift = &runtime;
	table.console_in = &input;
	short_path[24] = 0x5a;
	short_path[4] = 1U;
	short_path[41] = 2U;
	short_path[42] = END_DEVICE_PATH;
	short_path[43] = END_ENTIRE;
	short_path[44] = 4;
	partition_path[0] = 1;
	partition_path[1] = 1;
	partition_path[2] = 4;
	memcpy(partition_path + 4, short_path, 42);
	/* A short-form path identifies the partition by number and signature;
	 * geometry may legitimately change after a resize or move. */
	partition_path[12] = 0x40U;
	partition_path[20] = 0x80U;
	partition_path[46] = END_DEVICE_PATH;
	partition_path[47] = END_ENTIRE;
	partition_path[48] = 4;
	hob_list.handoff.version = 0U;
	current_boot_mode = BOOT_WITH_FULL_CONFIGURATION;
	failures += expect(cdk2_bds_entry((void *)0x1000, &table) ==
		EFI_COMPROMISED_DATA &&
		current_boot_mode == BOOT_WITH_FULL_CONFIGURATION,
		"BDS rejects a stale HOB-list version before using boot mode");
	hob_list.handoff.version = EFI_HOB_HANDOFF_TABLE_VERSION;
	hob_list.end.hob_type = EFI_HOB_TYPE_UNUSED;
	failures += expect(cdk2_bds_entry((void *)0x1000, &table) ==
		EFI_COMPROMISED_DATA &&
		current_boot_mode == BOOT_WITH_FULL_CONFIGURATION,
		"BDS rejects a missing terminal HOB before using boot mode");
	hob_list.end.hob_type = EFI_HOB_TYPE_END_OF_HOB_LIST;
	hob_list.handoff.efi_free_memory_bottom--;
	failures += expect(cdk2_bds_entry((void *)0x1000, &table) ==
		EFI_COMPROMISED_DATA &&
		current_boot_mode == BOOT_WITH_FULL_CONFIGURATION,
		"BDS rejects inconsistent HOB bounds before using boot mode");
	hob_list.handoff.efi_free_memory_bottom++;
	failures += expect(cdk2_bds_entry((void *)0x1000, &table) == EFI_SUCCESS &&
		installed != NULL && installed->boot_next_pending != NULL &&
		installed->enter_setup == cdk2_bds_enter_setup &&
		current_boot_mode == BOOT_ON_S4_RESUME &&
		os_support_sets == 1U &&
		(os_support_value & OS_INDICATIONS_BOOT_TO_FW_UI) == 0U,
		"driver entry installs callbacks and publishes only available setup support");
#if !CONFIG_CDK2_NATIVE_LVGL_SETUP
	{
		struct cdk2_lvgl_ui_protocol ui = { .show_form = fake_form };
		BOOLEAN presented = TRUE;
		UINTN native_before = native_browse_calls;

		expose_ui = TRUE;
		os_support_present = TRUE;
		os_support_value = OS_INDICATIONS_BOOT_TO_FW_UI | 4U | (1ULL << 6);
		failures += expect(!native_setup_available() &&
			browse_setup(&ui) == EFI_UNSUPPORTED &&
			native_browse_calls == native_before,
			"config-off setup is unavailable and never browsed");
		failures += expect(graphical_boot_order(NULL, 0U, NULL, FALSE,
			&presented, NULL) ==
			EFI_NOT_FOUND && !presented && menu_calls == 0U,
			"config-off setup is absent from the native menu");
		failures += expect(publish_os_indications_support(FALSE) == EFI_SUCCESS &&
			os_support_value == (4U | (1ULL << 6)),
			"config-off setup preserves capsule and unrelated support bits");
		os_indications_present = TRUE;
		os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
		failures += expect(EFI_ERROR(cdk2_bds_boot_once()) && deleted == 1U &&
			os_indications == OS_INDICATIONS_BOOT_TO_FW_UI,
			"unavailable firmware-UI request remains while BootNext proceeds");
	}
	if (failures == 0)
		puts("bds disabled setup tests passed");
	return failures != 0;
#endif
	{
		BOOLEAN pending = FALSE;

		failures += expect(installed->boot_next_pending(&pending) == EFI_SUCCESS &&
			pending,
			"BDS reports BootNext through its authoritative variable lookup");
		os_indications_present = TRUE;
		os_indications = OS_INDICATIONS_FILE_CAPSULE_DELIVERY;
		failures += expect(installed->boot_next_pending(&pending) == EFI_SUCCESS &&
			!pending,
			"S4 preserves capsule-associated BootNext for a normal boot");
		current_boot_mode = BOOT_WITH_FULL_CONFIGURATION;
		failures += expect(installed->boot_next_pending(&pending) == EFI_SUCCESS &&
			pending,
			"normal boot still consumes capsule-associated BootNext");
		os_indications_present = FALSE;
		os_indications = 0U;
	}
	os_support_set_status = EFI_UNSUPPORTED;
	os_support_present = FALSE;
	failures += expect(publish_os_indications_support(TRUE) == EFI_UNSUPPORTED,
		"unsupported support publication remains advisory");
	os_support_set_status = EFI_SUCCESS;
	os_support_present = TRUE;
	os_support_value = OS_INDICATIONS_BOOT_TO_FW_UI | 4U | (1ULL << 6);
	failures += expect(publish_os_indications_support(FALSE) == EFI_SUCCESS &&
		os_support_value == ((1ULL << 6) | 4U),
		"unavailable setup clears owned support bits only");
	failures += expect(publish_os_indications_support(TRUE) == EFI_SUCCESS &&
		os_support_value ==
		(OS_INDICATIONS_BOOT_TO_FW_UI | (1ULL << 6) | 4U),
		"support publication preserves existing indication bits");
	{
		UINTN sets = os_support_sets;

		failures += expect(publish_os_indications_support(TRUE) == EFI_SUCCESS &&
			os_support_sets == sets,
			"already-published support does not rewrite the variable");
	}
	failures += expect(wait_for_setup(1U) && key_reads == 2U && stalls == 1U,
		"boot timeout polls input before expiry");
	{
		UINTN old_stalls = stalls;

		key_reads = 0U;
		key_scan_code = 1U;
		failures += expect(!wait_for_setup(1U) && stalls == old_stalls + 10U,
			"non-F2 input cannot enter setup");
		key_scan_code = SCAN_F2;
	}
	{
		read_key_fn *saved_read = input.read_key;

		key_reads = 0U;
		queued_key_reads = 2U;
		scheduled_key = FALSE;
		drain_console_input();
		failures += expect(key_reads == 3U && queued_key_reads == 0U,
			"console drain consumes every queued key and stops at not-ready");
		key_reads = 0U;
		key_read_error = EFI_DEVICE_ERROR;
		drain_console_input();
		failures += expect(key_reads == 1U,
			"console drain stops at an input error");
		key_read_error = EFI_SUCCESS;
		input.read_key = NULL;
		drain_console_input();
		failures += expect(key_reads == 1U,
			"console drain tolerates a missing input callback");
		input.read_key = saved_read;
		scheduled_key = TRUE;
	}
	failures += expect(cdk2_bds_adjusted_setting_value(50U, 25U, 100U, 5U, -1) == 45U &&
			cdk2_bds_adjusted_setting_value(25U, 25U, 100U, 5U, -1) == 100U &&
			cdk2_bds_adjusted_setting_value(100U, 25U, 100U, 5U, 1) == 25U,
		"numeric settings support decrement and bounded cycling");
	{
		struct cdk2_lvgl_ui_protocol ui = { .show_form = fake_form };
		UINTN native_before = native_browse_calls;

		native_browse_status = EFI_ABORTED;
		failures += expect(browse_setup(&ui) == EFI_ABORTED &&
			native_browse_calls == native_before + 1U,
			"native settings cancellation returns directly");
		native_browse_status = EFI_DEVICE_ERROR;
		failures += expect(browse_setup(&ui) == EFI_DEVICE_ERROR &&
			native_browse_calls == native_before + 2U,
			"native settings errors return directly");
		native_browse_status = EFI_UNSUPPORTED;
		failures += expect(browse_setup(&ui) == EFI_UNSUPPORTED &&
			native_browse_calls == native_before + 3U,
			"unsupported native settings never fall back to HII");
	}
	failures += expect(!native_setup_available(),
		"missing native renderer is not advertised as firmware setup");
	expose_ui = TRUE;
	failures += expect(native_setup_available(),
		"native renderer and runtime services advertise firmware setup");
	{
		UINTN native_before = native_browse_calls;
		UINTN starts_before = battery_poll_starts;
		UINTN stops_before = battery_poll_stops;
		UINTN refreshes_before = battery_refreshes;

		key_reads = 0U;
		key_scan_code = SCAN_F2;
		native_browse_status = EFI_ABORTED;
		failures += expect(installed->enter_setup(1U, NULL, NULL) == EFI_SUCCESS &&
			native_browse_calls == native_before + 1U &&
			key_reads == 2U &&
			battery_poll_starts == starts_before + 1U &&
			battery_poll_stops == stops_before + 1U &&
			battery_refreshes == refreshes_before + 1U,
			"linear setup callback enters the native form and treats Escape as continuation");
		poll_stop_status = EFI_DEVICE_ERROR;
		key_reads = 0U;
		failures += expect(installed->enter_setup(1U, NULL, NULL) ==
			EFI_DEVICE_ERROR && native_browse_calls == native_before + 2U &&
			battery_poll_stops == stops_before + 2U,
			"setup teardown errors override Escape continuation");
		poll_stop_status = EFI_SUCCESS;
		os_indications_present = TRUE;
		os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
		key_reads = 0U;
		os_support_value &= ~OS_INDICATIONS_BOOT_TO_FW_UI;
		os_support_set_status = EFI_DEVICE_ERROR;
		failures += expect(installed->enter_setup(0U, NULL, NULL) ==
			EFI_DEVICE_ERROR && native_browse_calls == native_before + 2U &&
			os_indications == OS_INDICATIONS_BOOT_TO_FW_UI,
			"setup-support publication fails closed and preserves BootToFwUI");
		os_support_set_status = EFI_SUCCESS;
		native_browse_status = EFI_DEVICE_ERROR;
		key_reads = 0U;
		queued_key_reads = 2U;
		scheduled_key = FALSE;
		failures += expect(EFI_ERROR(installed->enter_setup(0U, NULL, NULL)) &&
			native_browse_calls == native_before + 3U && key_reads == 3U &&
			queued_key_reads == 0U &&
			os_indications == OS_INDICATIONS_BOOT_TO_FW_UI,
			"BootToFwUI drains stale input and preserves a failed request");
		native_browse_status = EFI_ABORTED;
		key_reads = 0U;
		poll_stop_status = EFI_DEVICE_ERROR;
		{
			UINTN indication_sets_before = os_indications_sets;
			failures += expect(installed->enter_setup(0U, NULL, NULL) ==
				EFI_DEVICE_ERROR && native_browse_calls == native_before + 4U &&
				key_reads == 1U && os_indications ==
				OS_INDICATIONS_BOOT_TO_FW_UI &&
				os_indications_sets == indication_sets_before,
				"failed Escape teardown preserves BootToFwUI without consuming it");
		}
		poll_stop_status = EFI_SUCCESS;
		key_reads = 0U;
		failures += expect(installed->enter_setup(0U, NULL, NULL) == EFI_SUCCESS &&
			native_browse_calls == native_before + 5U && key_reads == 1U &&
			os_indications == 0U,
			"successful setup teardown consumes BootToFwUI");
		scheduled_key = TRUE;
		key_reads = 0U;
		key_scan_code = 1U;
		{
			UINTN support_sets_before = os_support_sets;

		failures += expect(installed->enter_setup(1U, NULL, NULL) == EFI_NOT_READY &&
				native_browse_calls == native_before + 5U &&
			os_support_sets == support_sets_before,
			"linear setup callback rejects non-F2 input without browsing");
		}
		failures += expect(installed->enter_setup(0U, NULL, NULL) == EFI_NOT_READY &&
			native_browse_calls == native_before + 5U,
			"zero-duration setup window continues without polling or browsing");
		key_scan_code = SCAN_F2;
	}
	{
		UINTN native_before = native_browse_calls;
		UINTN stops_before = battery_poll_stops;

		expose_battery = FALSE;
		os_indications_present = TRUE;
		os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
		native_browse_status = EFI_SUCCESS;
		failures += expect(installed->enter_setup(0U, NULL, NULL) ==
			EFI_SUCCESS && native_browse_calls == native_before + 1U &&
			battery_poll_stops == stops_before + 1U && os_indications == 0U,
			"battery-less setup still restores UI after successful form");
		os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
		native_browse_status = EFI_ABORTED;
		failures += expect(installed->enter_setup(0U, NULL, NULL) ==
			EFI_SUCCESS && native_browse_calls == native_before + 2U &&
			battery_poll_stops == stops_before + 2U && os_indications == 0U,
			"battery-less Escape still restores UI before continuation");
		os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
		poll_stop_status = EFI_DEVICE_ERROR;
		failures += expect(installed->enter_setup(0U, NULL, NULL) ==
			EFI_DEVICE_ERROR && native_browse_calls == native_before + 3U &&
			battery_poll_stops == stops_before + 3U &&
			os_indications == OS_INDICATIONS_BOOT_TO_FW_UI,
			"battery-less restore failure preserves setup request");
		poll_stop_status = EFI_SUCCESS;
		expose_battery = TRUE;
		poll_begin_status = EFI_DEVICE_ERROR;
		native_browse_status = EFI_SUCCESS;
		os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
		failures += expect(installed->enter_setup(0U, NULL, NULL) ==
			EFI_DEVICE_ERROR && native_browse_calls == native_before + 3U &&
			battery_poll_stops == stops_before + 4U &&
			os_indications == OS_INDICATIONS_BOOT_TO_FW_UI,
			"poll start failure skips rendering and still tears down UI");
		poll_begin_status = EFI_SUCCESS;
		os_indications = 0U;
	}
	disable_ui_form = TRUE;
	failures += expect(!native_setup_available(),
		"renderer without form callback is not advertised");
	disable_ui_form = FALSE;
	disable_ui_menu = TRUE;
	failures += expect(!native_setup_available(),
		"renderer without menu callback is not advertised");
	disable_ui_menu = FALSE;
	{
		get_variable_fn *saved_get = runtime.get_variable;
		struct cdk2_lvgl_ui_protocol ui = { .show_form = fake_form };
		UINTN native_before = native_browse_calls;

		runtime.get_variable = NULL;
		failures += expect(!native_setup_available() &&
			browse_setup(&ui) == EFI_UNSUPPORTED &&
			native_browse_calls == native_before,
			"missing runtime storage neither advertises nor enters setup");
		runtime.get_variable = saved_get;
	}
	native_browse_status = EFI_NOT_FOUND;
	os_support_present = FALSE;
	os_support_set_status = EFI_DEVICE_ERROR;
	boot_status = cdk2_bds_boot_once();
	os_support_set_status = EFI_SUCCESS;
	failures += expect(EFI_ERROR(boot_status),
		"single BDS selection pass");
	failures += expect(battery_reads == 1U,
		"normal boot samples critical battery state once");
	failures += expect(menu_calls == 0U,
		"normal boot does not enter the graphical menu");
	short_option = (struct cdk2_bds_option){
		.attributes = 1, .path = short_path, .path_size = sizeof(short_path),
		.optional_data = load_option + sizeof(load_option) - 2U,
		.optional_data_size = 2U
	};
	failures += expect(option_path(&short_option, FALSE, TRUE) == EFI_SUCCESS,
		"expand short-form hard-drive path");
	memcpy(stale_full_path + 4U, short_path, sizeof(short_path));
	stale_full_option = (struct cdk2_bds_option){
		.attributes = 1, .path = stale_full_path,
		.path_size = sizeof(stale_full_path),
		.optional_data = load_option + sizeof(load_option) - 2U,
		.optional_data_size = 2U
	};
	failures += expect(option_path(&stale_full_option, FALSE, TRUE) == EFI_SUCCESS,
		"recover stale full hardware path by partition signature");
	failures += expect(connected == 3 && locate_all_calls == 0 && watchdogs == 1,
		"BDS connects selected paths without an all-device scan and disables watchdog");
	failures += expect(deleted == 1 && boot_current == 2 && loaded == 5 &&
		started == 5 && driver_loads == 1 && ready_created == 5 && ready_signaled == 5 &&
		ready_closed == 5,
		"BootNext is launched with policy, options, and boot events");
	failures += expect(capsules == 0U && capsule_deletes == 0U,
		"BDS leaves deferred capsules to the completed linear transaction");
	failures += expect(!os_support_present,
		"support publication failure is advisory to boot policy");
	{
		UINTN old_deleted = deleted;

		current_boot_mode = BOOT_ON_S4_RESUME;
		os_indications_present = TRUE;
		os_indications = OS_INDICATIONS_FILE_CAPSULE_DELIVERY;
		key_reads = 0U;
		key_scan_code = 1U;
		(void)cdk2_bds_boot_once();
		failures += expect(deleted == old_deleted &&
			os_indications == OS_INDICATIONS_FILE_CAPSULE_DELIVERY,
			"S4 defers disk capsule delivery without consuming BootNext");
		current_boot_mode = BOOT_WITH_FULL_CONFIGURATION;
		os_indications_present = FALSE;
		os_indications = 0U;
		key_scan_code = SCAN_F2;
	}
	{
		UINTN old_deleted = deleted, old_menu_calls = menu_calls;

		key_reads = 1U;
		expose_boot_order = TRUE;
		failures += expect(cdk2_bds_boot_once() == EFI_NOT_FOUND &&
			deleted == old_deleted && menu_calls == old_menu_calls +
			(CONFIG_CDK2_BOOT_TIMEOUT == 0U ? 0U : 1U),
			"timeout key enters the menu before consuming BootNext");
		failures += expect(battery_reads == 1U,
			"BDS retries do not poll battery state");
		expose_boot_order = FALSE;
	}
	os_indications_present = TRUE;
	os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
	expose_boot_order = TRUE;
	key_reads = 0U;
	queued_key_reads = 2U;
	scheduled_key = FALSE;
	menu_queued_keys = MAX_UINTN;
	{
		UINTN old_menu_calls = menu_calls, old_started = started;
		UINTN old_allocations = outstanding_allocations;

		os_indications_set_status = EFI_DEVICE_ERROR;
		require_fw_ui_clear_before_start = TRUE;
		failures += expect(cdk2_bds_boot_once() == EFI_DEVICE_ERROR &&
			menu_calls == old_menu_calls + 1U && started == old_started &&
			os_indications == OS_INDICATIONS_BOOT_TO_FW_UI &&
			menu_queued_keys == 0U && queued_key_reads == 0U &&
			outstanding_allocations == old_allocations,
			"legacy BootToFwUI drains stale input before menu presentation");
		os_indications_set_status = EFI_NOT_READY;
		old_menu_calls = menu_calls;
		failures += expect(cdk2_bds_boot_once() == EFI_NOT_READY &&
			menu_calls == old_menu_calls + 1U && started == old_started &&
			os_indications == OS_INDICATIONS_BOOT_TO_FW_UI &&
			outstanding_allocations == old_allocations,
			"not-ready consumption error is not mistaken for pagination");
		os_indications_set_status = EFI_SUCCESS;
		old_menu_calls = menu_calls;
		jump_on_start = TRUE;
		if (setjmp(start_jump) == 0) {
			(void)cdk2_bds_boot_once();
			failures += expect(FALSE,
				"selected image reaches non-returning StartImage sentinel");
		} else {
			UINTN allocation;

			for (allocation = 0U; allocation < start_allocation_count;
			     allocation++)
				if (start_allocations[allocation] != NULL) {
					free(start_allocations[allocation]);
					outstanding_allocations--;
				}
			start_allocation_count = 0U;
			failures += expect(menu_calls == old_menu_calls + 1U &&
				started == old_started && os_indications == 0U &&
				start_with_fw_ui_pending == 0U,
				"request is clear before non-returning StartImage");
		}
		jump_on_start = FALSE;
		require_fw_ui_clear_before_start = FALSE;
	}
	os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
	{
		UINTN old_menu_calls = menu_calls;

		menu_status = EFI_DEVICE_ERROR;
		failures += expect(EFI_ERROR(cdk2_bds_boot_once()) &&
			menu_calls == old_menu_calls + 1U &&
			os_indications == OS_INDICATIONS_BOOT_TO_FW_UI,
			"failed menu presentation preserves firmware-UI request");
		menu_status = EFI_SUCCESS;
	}
	{
		UINTN drift;
		get_variable_fn *saved_get = runtime.get_variable;
		set_variable_fn *saved_set = runtime.set_variable;

		for (drift = 0U; drift < 5U; drift++) {
			UINTN old_started = started, old_allocations = outstanding_allocations;

			ui_locates = 0U;
			fail_ui_locate_at = drift == 0U ? 2U : 0U;
			remove_ui_menu_at = drift == 1U ? 2U : 0U;
			remove_ui_form_at = drift == 2U ? 2U : 0U;
			remove_runtime_get_at = drift == 3U ? 2U : 0U;
			remove_runtime_set_at = drift == 4U ? 2U : 0U;
			runtime.get_variable = saved_get;
			runtime.set_variable = saved_set;
			os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
			failures += expect(cdk2_bds_boot_once() == EFI_UNSUPPORTED &&
				started == old_started &&
				os_indications == OS_INDICATIONS_BOOT_TO_FW_UI &&
				outstanding_allocations == old_allocations,
				"capability drift preserves request and prevents launch");
		}
		fail_ui_locate_at = 0U;
		remove_ui_menu_at = 0U;
		remove_ui_form_at = 0U;
		remove_runtime_get_at = 0U;
		remove_runtime_set_at = 0U;
		runtime.get_variable = saved_get;
		runtime.set_variable = saved_set;
	}
	menu_select_setup_once = TRUE;
	{
		UINTN old_menu_calls = menu_calls;

		failures += expect(cdk2_bds_boot_once() == EFI_NOT_FOUND &&
		menu_calls == old_menu_calls + 1U &&
		os_indications == 0U,
			"presented native menu consumes firmware-UI request");
	}
	failures += expect(native_browse_calls == 12U &&
		battery_poll_starts == 7U && battery_poll_stops == 10U &&
		battery_refreshes == 6U,
		"firmware-UI request browses native settings with battery refresh");
	{
		UINT16 order[] = { 1U };
		UINTN resume = 0U;
		UINTN native_before = native_browse_calls;
		UINTN stops_before = battery_poll_stops;

		os_indications_present = TRUE;
		os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
		menu_select_setup_once = TRUE;
		native_browse_status = EFI_ABORTED;
		poll_stop_status = EFI_DEVICE_ERROR;
		failures += expect(graphical_boot_order(order, ARRAY_SIZE(order), &resume,
			FALSE, NULL, NULL) == EFI_DEVICE_ERROR &&
			native_browse_calls == native_before + 1U &&
			battery_poll_stops == stops_before + 1U &&
			os_indications == OS_INDICATIONS_BOOT_TO_FW_UI,
			"graphical setup propagates teardown failure after Escape");
		poll_stop_status = EFI_SUCCESS;
		os_indications = 0U;
		os_indications_present = TRUE;
	}
	{
		UINT16 order[] = { 1U };
		UINTN native_before = native_browse_calls;
		UINTN stops_before = battery_poll_stops;
		expose_battery = FALSE;
		menu_select_setup_once = TRUE;
		native_browse_status = EFI_SUCCESS;
		failures += expect(graphical_boot_order(order, ARRAY_SIZE(order), NULL,
			FALSE, NULL, NULL) == EFI_SUCCESS &&
			native_browse_calls == native_before + 1U &&
			battery_poll_stops == stops_before + 1U,
			"graphical battery-less setup retains the UI teardown owner");
		native_browse_status = EFI_ABORTED;
		poll_stop_status = EFI_DEVICE_ERROR;
		menu_select_setup_once = TRUE;
		failures += expect(graphical_boot_order(order, ARRAY_SIZE(order), NULL,
			FALSE, NULL, NULL) == EFI_DEVICE_ERROR &&
			native_browse_calls == native_before + 2U &&
			battery_poll_stops == stops_before + 2U,
			"graphical battery-less Escape propagates restore failure");
		poll_stop_status = EFI_SUCCESS;
		expose_battery = TRUE;
	}
	os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
	menu_cancel_once = TRUE;
	{
		UINTN old_menu_calls = menu_calls;

		failures += expect(EFI_ERROR(cdk2_bds_boot_once()) &&
			menu_calls == old_menu_calls + 1U && os_indications == 0U,
			"presented menu cancellation consumes firmware-UI request");
	}
	scheduled_key = TRUE;
	os_indications_present = FALSE;
	expose_boot_order = FALSE;
	{
		UINT16 order[33] = { 0 };
		UINTN resume = 0U;
		UINTN old_loaded = loaded, old_started = started;
		UINTN old_created = ready_created, old_signaled = ready_signaled;
		UINTN old_closed = ready_closed;

		order[32] = 1U;
		failures += expect(graphical_boot_order(order, ARRAY_SIZE(order), &resume,
			FALSE, NULL, NULL) ==
			EFI_ABORTED && resume == ARRAY_SIZE(order) &&
			menu_item_count == 2U &&
			loaded == old_loaded + 1U && started == old_started + 1U &&
			ready_created == old_created + 1U &&
			ready_signaled == old_signaled + 1U &&
			ready_closed == old_closed + 1U && boot_current >= 4U,
			"LVGL scans the full BootOrder and launches through BootCurrent");
		for (resume = 0U; resume < ARRAY_SIZE(order); resume++)
			order[resume] = 1U;
		failures += expect(graphical_boot_order(order, ARRAY_SIZE(order), &resume,
			FALSE, NULL, NULL) ==
			EFI_ABORTED && menu_item_count == CDK2_LVGL_MENU_MAX_ITEMS,
			"LVGL reserves a menu slot for Firmware Setup");
		menu_select_next_once = TRUE;
		os_indications_present = TRUE;
		os_indications = OS_INDICATIONS_BOOT_TO_FW_UI;
		{
			UINTN old_sets = os_indications_sets;
			BOOLEAN presented = FALSE, consumed = FALSE;

		failures += expect(graphical_boot_order(order, ARRAY_SIZE(order), &resume,
			TRUE, &presented, &consumed) == EFI_ABORTED && resume == 31U &&
			!menu_select_next_once && presented && consumed &&
			os_indications_sets == old_sets + 1U && os_indications == 0U,
			"pagination consumes firmware-UI request exactly once");
		}
	}
	{
		static const CHAR16 pending[] = L"CapsuleUpdateData";
		UINTN old_capsules = capsules, old_deletes = capsule_deletes;
		UINTN old_enum = capsule_fixture_enum_calls;

		capsule_fixture[0] = (struct capsule_fixture_variable) {
			.name = pending, .guid = capsule_vendor_guid,
			.scatter = 0x12345678U, .present = TRUE };
		capsule_fixture_count = 1U;
		capsule_fixture_enabled = TRUE;
		os_indications_present = TRUE;
		os_indications = OS_INDICATIONS_FILE_CAPSULE_DELIVERY;
		current_boot_mode = BOOT_WITH_FULL_CONFIGURATION;
		(void)cdk2_bds_boot_once();
		failures += expect(capsules == old_capsules && capsule_deletes == old_deletes &&
			capsule_fixture_enum_calls == old_enum && capsule_fixture[0].present &&
			os_indications == OS_INDICATIONS_FILE_CAPSULE_DELIVERY,
			"BDS does not replay or retire the linear capsule transaction");
		capsule_fixture_enabled = FALSE;
	}
	if (failures == 0)
		puts("bds entry tests passed");
	return failures != 0;
}
