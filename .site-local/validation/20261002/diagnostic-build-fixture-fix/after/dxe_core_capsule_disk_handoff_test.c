/* SPDX-License-Identifier: GPL-2.0-only */

/* Exercise the real linear CAPSULE_DISK phase with an EFI-style filesystem.
 * The fixture counts open_volume calls: two calls are expected here, one for
 * discovery and one for the transaction completion cleanup. */
#include <cdk2/pci_topology_handoff.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "capsule_delivery_policy_fixture.h"

#define cdk2_dxe_validate_boot_file cdk2_capsule_test_validate_boot_file
#include "../src/modules/dxe_core/private_control.h"
#define static
#ifdef CDK2_DXE_CAPSULE_DISK_SOURCE
#include CDK2_DXE_CAPSULE_DISK_SOURCE
#else
#include "../src/modules/dxe_core/entry.c"
#endif
#undef static
#undef cdk2_dxe_validate_boot_file

#define IMAGE_SIZE (sizeof(struct cdk2_capsule_header) + 4U)

static UINT64 support_bits = 1ULL | (1ULL << 6);
static UINT32 support_attributes = 6;
static UINTN support_size = sizeof(support_bits), support_writes;
static EFI_STATUS support_get_status = EFI_SUCCESS, support_set_status = EFI_SUCCESS;

static EFI_STATUS CDK2_MS_ABI support_get(CHAR16 *name, EFI_GUID *guid, UINT32 *attributes,
					  UINTN *size, void *data)
{
	static const EFI_GUID global = {
		0x8be4df61, 0x93ca, 0x11d2, {0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c}
	};

	if (memcmp(name, L"OsIndicationsSupported", sizeof(L"OsIndicationsSupported")) ||
	    memcmp(guid, &global, sizeof(global)))
		return EFI_NOT_FOUND;
	if (EFI_ERROR(support_get_status))
		return support_get_status;
	if (*size < sizeof(support_bits))
		return EFI_BUFFER_TOO_SMALL;
	memcpy(data, &support_bits, sizeof(support_bits));
	*size = support_size;
	*attributes = support_attributes;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI support_set(CHAR16 *name, EFI_GUID *guid, UINT32 attributes,
					  UINTN size, void *data)
{
	(void)name;
	(void)guid;
	if (attributes != 6 || size != sizeof(support_bits))
		return EFI_INVALID_PARAMETER;
	if (EFI_ERROR(support_set_status))
		return support_set_status;
	memcpy(&support_bits, data, sizeof(support_bits));
	support_writes++;
	return EFI_SUCCESS;
}

static const EFI_GUID capsule_boot_guid = {
	0x2890d42b,
	0x7f9b,
	0x4a66,
	{0x9a, 0x12, 0x54, 0x8d, 0x6b, 0x57, 0xd8, 0x20}
};

struct test_file_info {
	UINT64 size, file_size, physical_size;
	UINT8 times[48];
	UINT64 attributes;
	CHAR16 name[32];
};

struct filesystem_fixture {
	struct cdk2_fat_simple_fs_protocol filesystem;
	struct cdk2_fat_file_protocol root, directory, file;
	struct cdk2_capsule_header capsule;
	UINT8 payload[4];
	UINTN open_volumes, directory_reads, file_reads, deletes;
	UINTN file_offset;
};

typedef struct cdk2_fat_file_protocol **file_result_ptr;
typedef CHAR16 *char16_ptr;
typedef UINT32 *uint32_ptr;

static struct filesystem_fixture *active_fixture;

static EFI_STATUS CDK2_MS_ABI fixture_close(struct cdk2_fat_file_protocol *file)
{
	return file == &active_fixture->root || file == &active_fixture->directory ||
			       file == &active_fixture->file ?
		       EFI_SUCCESS :
		       EFI_INVALID_PARAMETER;
}

static EFI_STATUS CDK2_MS_ABI fixture_delete(struct cdk2_fat_file_protocol *file)
{
	if (file != &active_fixture->file)
		return EFI_INVALID_PARAMETER;
	active_fixture->deletes++;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fixture_open(struct cdk2_fat_file_protocol *file,
					   file_result_ptr opened, char16_ptr name, UINT64 mode,
					   UINT64 attributes)
{
	static const CHAR16 directory_name[] = L"\\EFI\\UpdateCapsule";

	(void)attributes;
	if (file == &active_fixture->root && mode == 1U &&
	    memcmp(name, directory_name, sizeof(directory_name)) == 0) {
		*opened = &active_fixture->directory;
		return EFI_SUCCESS;
	}
	if (file == &active_fixture->directory && (mode == 1U || mode == 3U)) {
		active_fixture->file_offset = 0U;
		*opened = &active_fixture->file;
		return EFI_SUCCESS;
	}
	return EFI_NOT_FOUND;
}

static EFI_STATUS CDK2_MS_ABI fixture_read(struct cdk2_fat_file_protocol *file, UINTN *size,
					   void *buffer)
{
	if (size == NULL || buffer == NULL)
		return EFI_INVALID_PARAMETER;
	if (file == &active_fixture->directory) {
		struct test_file_info *info = buffer;

		active_fixture->directory_reads++;
		if (active_fixture->directory_reads > 1U) {
			*size = 0U;
			return EFI_SUCCESS;
		}
		if (*size < offsetof(struct test_file_info, name) + sizeof(CHAR16) * 2U)
			return EFI_BUFFER_TOO_SMALL;
		memset(info, 0, *size);
		info->size = offsetof(struct test_file_info, name) + sizeof(CHAR16) * 2U;
		info->file_size = IMAGE_SIZE;
		info->attributes = CDK2_CAPSULE_FILE_ARCHIVE;
		info->name[0] = L'c';
		info->name[1] = L'\0';
		*size = info->size;
		return EFI_SUCCESS;
	}
	if (file != &active_fixture->file)
		return EFI_INVALID_PARAMETER;
	active_fixture->file_reads++;
	if (active_fixture->file_offset >= IMAGE_SIZE) {
		*size = 0U;
		return EFI_SUCCESS;
	}
	if (*size > IMAGE_SIZE - active_fixture->file_offset)
		*size = IMAGE_SIZE - active_fixture->file_offset;
	memcpy(buffer, (UINT8 *)&active_fixture->capsule + active_fixture->file_offset, *size);
	active_fixture->file_offset += *size;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fixture_get_info(struct cdk2_fat_file_protocol *file,
					       EFI_GUID *guid, UINTN *size, void *buffer)
{
	struct test_file_info *info = buffer;

	(void)guid;
	if (file != &active_fixture->file || size == NULL || buffer == NULL ||
	    *size < sizeof(*info))
		return EFI_INVALID_PARAMETER;
	memset(info, 0, sizeof(*info));
	info->size = sizeof(*info);
	info->file_size = IMAGE_SIZE;
	info->attributes = CDK2_CAPSULE_FILE_ARCHIVE;
	*size = sizeof(*info);
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI fixture_open_volume(
	struct cdk2_fat_simple_fs_protocol *filesystem, struct cdk2_fat_file_protocol **root)
{
	if (filesystem != &active_fixture->filesystem || root == NULL)
		return EFI_INVALID_PARAMETER;
	active_fixture->open_volumes++;
	*root = &active_fixture->root;
	return EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI
capsule_process_scatter(struct cdk2_capsule_boot_protocol *processor, UINT64 scatter)
{
	const UINT64 *blocks = (const UINT64 *)(UINTN)scatter;

	(void)processor;
	return blocks[0] == IMAGE_SIZE && blocks[2] == 0U ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}

static EFI_STATUS CDK2_MS_ABI capsule_validate(struct cdk2_capsule_boot_protocol *processor,
					       const void *image, UINTN size)
{
	(void)processor;
	return image != NULL && size == IMAGE_SIZE ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}

static EFI_STATUS CDK2_MS_ABI test_allocate_pool(UINT32 type, UINTN size, void **buffer)
{
	(void)type;
	if (buffer == NULL)
		return EFI_INVALID_PARAMETER;
	*buffer = malloc(size);
	return *buffer == NULL ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

static EFI_STATUS CDK2_MS_ABI test_free_pool(void *buffer)
{
	free(buffer);
	return EFI_SUCCESS;
}

EFI_STATUS cdk2_capsule_test_validate_boot_file(struct cdk2_fat_simple_fs_protocol *filesystem,
						char16_ptr path, uint32_ptr file_size)
{
	(void)filesystem;
	(void)path;
	if (file_size != NULL)
		*file_size = 0U;
	return EFI_NOT_FOUND;
}

static int expect(BOOLEAN condition, const char *message)
{
	if (!condition)
		fprintf(stderr, "FAIL: %s\n", message);
	return condition ? 0 : 1;
}

int main(void)
{
	struct filesystem_fixture fixture = {0};
	struct cdk2_capsule_boot_protocol processor = {
		.process_scatter = capsule_process_scatter,
		.validate_capsule = capsule_validate,
	};
	struct cdk2_linear_state state;
	struct dxe_services_context context = {0};
	struct cdk2_dxe_runtime_services runtime = {
		.get_variable = support_get,
		.set_variable = support_set,
	};
	__typeof__(*core.runtime_storage) runtime_storage = {0};
	struct test_capsule_delivery_hobs hobs;
	void *esp = (void *)0x1000U;
	EFI_STATUS status;
	int failures = 0;

	fixture.filesystem.open_volume = fixture_open_volume;
	fixture.root.open = fixture_open;
	fixture.root.close = fixture_close;
	fixture.directory.open = fixture_open;
	fixture.directory.close = fixture_close;
	fixture.directory.read = fixture_read;
	fixture.file.close = fixture_close;
	fixture.file.delete = fixture_delete;
	fixture.file.read = fixture_read;
	fixture.file.get_info = fixture_get_info;
	fixture.capsule.header_size = sizeof(fixture.capsule);
	fixture.capsule.image_size = IMAGE_SIZE;
	fixture.capsule.flags = CDK2_CAPSULE_PERSIST_ACROSS_RESET;
	fixture.capsule.guid = (EFI_GUID){
		1U, 2U, 3U, {4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U}
	};
	memcpy(fixture.payload, "CDK2", sizeof(fixture.payload));
	active_fixture = &fixture;

	memset(&core, 0, sizeof(core));
	status = cdk2_dxe_database_initialize(&core.database);
	failures += expect(status == EFI_SUCCESS, "database initialization");
	core.boot_services.allocate_pool = (void *)test_allocate_pool;
	core.boot_services.free_pool = (void *)test_free_pool;
	failures += expect(cdk2_dxe_restore(&core.database, esp, &simple_fs_protocol,
					    &fixture.filesystem) == EFI_SUCCESS,
			   "selected ESP filesystem publication");
	failures += expect(cdk2_dxe_restore(&core.database, esp, &capsule_boot_guid,
					    &processor) == EFI_SUCCESS,
			   "capsule processor publication");
	selected_esp = esp;
	context.handoff = &hobs.handoff;
	context.linear_state = &state;
	core.runtime_storage = &runtime_storage;
	core.runtime_storage->system_table.runtime_services = &runtime;
	for (uint32_t transports = 0;
	     transports <= (CB_CAPSULE_DELIVERY_RAM | CB_CAPSULE_DELIVERY_DISK); transports++) {
		test_capsule_delivery_hobs_init(&hobs, transports, transports ? 1048576 : 0,
						transports ? 1048576 : 0);
		cdk2_linear_state_init(&state);
		state.next_phase = CDK2_LINEAR_CAPSULE_DECIDE;
		state.completed_mask = (1U << CDK2_LINEAR_CAPSULE_DECIDE) - 1U;
		status = decide_capsule_path(&context);
		failures += expect(
			status == EFI_SUCCESS &&
				context.capsule_outcome ==
					((transports & CB_CAPSULE_DELIVERY_DISK) ?
						 CDK2_LINEAR_CAPSULE_RESULT_DISK_DEFERRED :
						 CDK2_LINEAR_CAPSULE_RESULT_NONE),
			"actual owned policy decides capsule transports");
		failures += expect(
			support_bits == (1ULL | (1ULL << 6) |
					 ((transports & CB_CAPSULE_DELIVERY_DISK) ? 4ULL : 0)),
			"owned disk capability alone controls bit4, preserving setup and unrelated bits");
	}
	test_capsule_delivery_hobs_init(&hobs, CB_CAPSULE_DELIVERY_DISK, 1048576, 1048576);
	hobs.handoff.boot_mode = BOOT_ON_S4_RESUME;
	cdk2_linear_state_init(&state);
	state.next_phase = CDK2_LINEAR_CAPSULE_DECIDE;
	state.completed_mask = (1U << CDK2_LINEAR_CAPSULE_DECIDE) - 1U;
	status = decide_capsule_path(&context);
	failures += expect(status == EFI_SUCCESS &&
				   context.capsule_outcome == CDK2_LINEAR_CAPSULE_RESULT_NONE,
			   "S4 does not consume disk delivery");
	failures += expect((support_bits & 4ULL) != 0,
			   "S4 publishes capability without consuming a pending update");
	test_capsule_delivery_hobs_init(&hobs, CB_CAPSULE_DELIVERY_DISK, 1048576, 1048576);
	hobs.policy.reserved[0] = 1;
	cdk2_linear_state_init(&state);
	state.next_phase = CDK2_LINEAR_CAPSULE_DECIDE;
	state.completed_mask = (1U << CDK2_LINEAR_CAPSULE_DECIDE) - 1U;
	status = decide_capsule_path(&context);
	failures += expect(status == EFI_SUCCESS &&
				   context.capsule_outcome == CDK2_LINEAR_CAPSULE_RESULT_NONE,
			   "malformed delivery policy disables capsules");
	failures += expect(support_bits == (1ULL | (1ULL << 6)),
			   "malformed delivery policy clears only obsolete disk support");
	{
		UINTN old_writes;

		test_capsule_delivery_hobs_init(&hobs, CB_CAPSULE_DELIVERY_DISK, 1048576,
						1048576);
		hobs.policy_hob.name.data1 ^= 1;
		support_bits |= 4;
		cdk2_linear_state_init(&state);
		state.next_phase = CDK2_LINEAR_CAPSULE_DECIDE;
		state.completed_mask = (1U << CDK2_LINEAR_CAPSULE_DECIDE) - 1U;
		failures += expect(decide_capsule_path(&context) == EFI_SUCCESS &&
					   support_bits == (1ULL | (1ULL << 6)),
				   "absent policy clears only obsolete disk support");
		old_writes = support_writes;
		cdk2_linear_state_init(&state);
		state.next_phase = CDK2_LINEAR_CAPSULE_DECIDE;
		state.completed_mask = (1U << CDK2_LINEAR_CAPSULE_DECIDE) - 1U;
		failures += expect(decide_capsule_path(&context) == EFI_SUCCESS &&
					   support_writes == old_writes,
				   "unchanged owned support is not rewritten");
	}
	{
		UINTN old_writes = support_writes;

		test_capsule_delivery_hobs_init(&hobs, CB_CAPSULE_DELIVERY_DISK, 1048576,
						1048576);
		support_attributes = 7;
		failures += expect(decide_capsule_path(&context) == EFI_COMPROMISED_DATA &&
					   support_writes == old_writes,
				   "wrong support attributes refuse publication");
		support_attributes = 6;
		support_size = 4;
		failures += expect(decide_capsule_path(&context) == EFI_COMPROMISED_DATA &&
					   support_writes == old_writes,
				   "wrong support length refuses publication");
		support_size = sizeof(support_bits);
		support_get_status = EFI_DEVICE_ERROR;
		failures += expect(decide_capsule_path(&context) == EFI_DEVICE_ERROR &&
					   support_writes == old_writes,
				   "support read failure propagates without writes");
		support_get_status = EFI_SUCCESS;
		support_set_status = EFIERR(8);
		failures += expect(decide_capsule_path(&context) == EFIERR(8) &&
					   support_writes == old_writes,
				   "support write failure propagates without capsule dispatch");
		support_set_status = EFI_SUCCESS;
		support_get_status = EFI_NOT_FOUND;
		cdk2_linear_state_init(&state);
		state.next_phase = CDK2_LINEAR_CAPSULE_DECIDE;
		state.completed_mask = (1U << CDK2_LINEAR_CAPSULE_DECIDE) - 1U;
		failures += expect(
			decide_capsule_path(&context) == EFI_SUCCESS && support_bits == 4,
			"missing support starts from zero and publishes only owned disk capability");
		support_get_status = EFI_SUCCESS;
	}
	test_capsule_delivery_hobs_init(&hobs, CB_CAPSULE_DELIVERY_DISK, 1048576, 1048576);
	failures += expect(cdk2_capsule_delivery_policy_from_hobs(
				   &hobs, &processor.delivery_policy) == EFI_SUCCESS,
			   "owned processor delivery policy");

	cdk2_linear_state_init(&state);
	state.next_phase = CDK2_LINEAR_CAPSULE_DECIDE;
	state.completed_mask = (1U << CDK2_LINEAR_CAPSULE_DECIDE) - 1U;
	status = cdk2_linear_capsule_gate_early(&state, FALSE, TRUE, &context.capsule_outcome);
	failures += expect(status == EFI_SUCCESS &&
				   context.capsule_outcome ==
					   CDK2_LINEAR_CAPSULE_RESULT_DISK_DEFERRED,
			   "disk-deferred capsule gate");
	state.next_phase = CDK2_LINEAR_CAPSULE_DISK;
	state.completed_mask = (1U << CDK2_LINEAR_CAPSULE_DISK) - 1U;
	context.linear_state = &state;
	processor.delivery_policy.allowed_transports = CB_CAPSULE_DELIVERY_RAM;
	status = process_disk_capsules(&context);
	failures += expect(status == EFI_UNSUPPORTED && fixture.open_volumes == 0 &&
				   fixture.directory_reads == 0 && fixture.deletes == 0,
			   "disabled disk policy has no filesystem side effects");
	processor.delivery_policy.allowed_transports = CB_CAPSULE_DELIVERY_DISK;
	status = process_disk_capsules(&context);
	failures += expect(status == EFI_SUCCESS, "disk capsule phase");
	failures += expect(fixture.open_volumes == 2U,
			   "one discovery open plus one intentional completion reopen");
	failures +=
		expect(fixture.directory_reads == 2U, "exactly one directory discovery scan");
	failures += expect(fixture.deletes == 1U,
			   "completion cleanup removed the discovered capsule once");
	failures +=
		expect(cdk2_linear_capsule_outcome(&state) == CDK2_LINEAR_CAPSULE_RESULT_DISK,
		       "disk capsule outcome finalized");

	puts(failures == 0 ? "DXE capsule disk selected-ESP scan test: PASS" :
			     "DXE capsule disk selected-ESP scan test: FAIL");
	return failures != 0;
}
