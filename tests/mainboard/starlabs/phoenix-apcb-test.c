/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#define main coreboot_main
#include <mainboard/starlabs/phoenix/apcb.c>
#undef main
#include "psp_def.h"

#define BODY_SIZE 128
#define SLOT_SIZE 256
static uint8_t image[3 * SLOT_SIZE], original[BODY_SIZE + HMAC_SIZE];
static struct mem_region_device flash = MEM_REGION_DEV_RW_INIT(image, sizeof(image));
static int current;
static unsigned int validations, signatures;
static bool unknown_ancestry, timeout;

const struct region_device *boot_device_ro(void)
{
	return &flash.rdev;
}

const struct region_device *boot_device_rw(void)
{
	return &flash.rdev;
}

void clflush_region(uintptr_t start, size_t size)
{
}

enum soc_type get_soc_type(void)
{
	return SOC_PHOENIX;
}

enum cb_err psp_get_recovery_mode(bool *recovery)
{
	if (current < 0)
		return CB_ERR;
	*recovery = current == 1;
	return CB_SUCCESS;
}

void *_cbfs_default_allocator(void *arg, size_t size, const union cbfs_mdata *unused)
{
	struct _cbfs_default_allocator_arg *dest = arg;
	assert_true(size <= dest->buf_size);
	return dest->buf;
}

void *_cbfs_alloc(const char *name, cbfs_allocator_t allocator, void *arg, size_t *size,
		  bool force_ro, enum cbfs_type *type)
{
	assert_string_equal(name, "apcb-factory");
	*size = sizeof(original);
	return memcpy(allocator(arg, *size, NULL), original, *size);
}

int fmap_locate_area(const char *name, struct region *area)
{
	assert_string_equal(name, "AMDFW");
	*area = region_create(SLOT_SIZE, 2 * SLOT_SIZE);
	return 0;
}

enum cb_err psp_fw_read_and_validate_table(const struct region_device *rd, uint32_t offset,
					  enum psp_dir_type type, struct psp_dir_region *dir)
{
	assert_int_equal(type, PSP_DIR_EFS);
	return CB_SUCCESS;
}

uint32_t psp_dir_entry_offset(const struct psp_dir_region *dir, const union psp_dir_entry *e)
{
	return e->bios.source;
}

char psp_fw_partition(const struct psp_dir_region *dir)
{
	return unknown_ancestry ? 0 : 'A' + dir->parent_index;
}

bool psp_dir_walk_recursive(const struct psp_dir_region *dir, size_t level, uint32_t chip_id,
			    psp_dir_walk_hdr_callback hdr, psp_dir_walk_callback entry, void *arg)
{
	const struct psp_dir_region ish = { .type = PSP_DIR_ISH };
	const struct psp_dir_region psp = { .type = PSP_DIR_PSP, .level = 2, .parent = &ish };
	struct psp_dir_region bios = { .type = PSP_DIR_BIOS, .level = 2, .parent = &psp };
	union psp_dir_entry primary = { .bios = {
		.type = AMD_BIOS_APCB, .writable = 1, .size = SLOT_SIZE,
	} };

	assert_int_equal(chip_id, 0xbc0d0400);
	/* Visit B first to distinguish actual selection from traversal order. */
	for (int i = 1; i >= 0; i--) {
		bios.parent_index = i;
		primary.bios.source = (i + 1) * SLOT_SIZE;
		if (!entry(&bios, 0, 0, &primary, arg))
			return false;
	}
	return true;
}

void __noreturn die(const char *message, ...)
{
	mock_assert(false, message, __FILE__, __LINE__);
	__builtin_unreachable();
}

int send_psp_command(uint32_t command, void *buffer)
{
	struct mbox_cmd_hmac_data_buffer *req = buffer;
	uint8_t *body = (void *)(uintptr_t)req->buffer;
	uint8_t *mac = body + req->signature_offset;

	assert_int_equal(command, MBOX_BIOS_CMD_VALIDATE_HMAC_PRE_SMM);
	assert_int_equal(req->data_size, BODY_SIZE);
	assert_int_equal(req->signature_offset, BODY_SIZE);
	assert_memory_equal(body, original, BODY_SIZE);
	if (timeout)
		return -PSPSTS_CMD_TIMEOUT;
	if (req->validate) {
		validations++;
		for (size_t i = 0; i < HMAC_SIZE; i++)
			if (mac[i] != 0x5a)
				req->header.status = 1;
	} else {
		signatures++;
		memset(mac, 0x5a, HMAC_SIZE);
	}
	return 0;
}

static int setup(void **state)
{
	memset(original, 0, sizeof(original));
	memcpy(original, "APCB", 4);
	write_le16(original + 4, BODY_SIZE);
	write_le16(original + 6, 0x30);
	write_le32(original + 8, BODY_SIZE);
	for (size_t i = 0; i < 12; i++)
		original[12] -= original[i];
	memset(original + BODY_SIZE, 0xff, HMAC_SIZE);
	memcpy(image + SLOT_SIZE, original, sizeof(original));
	memcpy(image + 2 * SLOT_SIZE, original, sizeof(original));
	current = 0;
	unknown_ancestry = timeout = false;
	validations = signatures = 0;
	authenticated_at_entry = false;
	pending_slot = 0;
	return 0;
}

static void selected_slot(void **state)
{
	current = (intptr_t)*state;
	uint8_t *active = image + (current + 1) * SLOT_SIZE + BODY_SIZE;
	uint8_t *inactive = image + (2 - current) * SLOT_SIZE + BODY_SIZE;

	prepare(NULL);
	commit(NULL);
	assert_int_equal(signatures, 1);
	assert_int_equal(validations, 1);
	assert_false(authenticated_at_entry);
	for (size_t i = 0; i < HMAC_SIZE; i++) {
		assert_int_equal(active[i], 0x5a);
		assert_int_equal(inactive[i], 0xff);
	}

	/* The authenticated active bank permits reuse on the following boot. */
	prepare(NULL);
	assert_int_equal(pending_slot, 0);
	assert_true(authenticated_at_entry);
	assert_int_equal(signatures, 1);
}

static void partition_changed(void **state)
{
	prepare(NULL);
	assert_int_equal(pending_slot, SLOT_SIZE);
	current = 1;
	commit(NULL);
	assert_int_equal(pending_slot, 0);
	assert_memory_equal(image + SLOT_SIZE, original, sizeof(original));
	assert_memory_equal(image + 2 * SLOT_SIZE, original, sizeof(original));
}

static void existing_macs(void **state)
{
	memset(image + SLOT_SIZE + BODY_SIZE, 0x5a, HMAC_SIZE);
	memset(image + 2 * SLOT_SIZE + BODY_SIZE, 0x5a, HMAC_SIZE);
	prepare(NULL);
	assert_true(authenticated_at_entry);
	assert_int_equal(validations, 1);
	assert_int_equal(signatures, 0);
	assert_int_equal(pending_slot, 0);
}

static void invalid_existing_mac(void **state)
{
	current = (intptr_t)*state;
	image[(current + 1) * SLOT_SIZE + BODY_SIZE] = 0;
	prepare(NULL);
	assert_false(authenticated_at_entry);
	assert_int_equal(validations, 1);
	assert_int_equal(signatures, 0);
	assert_int_equal(pending_slot, 0);
}

static void invalid_inactive_bank(void **state)
{
	current = (intptr_t)*state;
	memset(image + (current + 1) * SLOT_SIZE + BODY_SIZE, 0x5a, HMAC_SIZE);
	memset(image + (2 - current) * SLOT_SIZE, 0, SLOT_SIZE);
	prepare(NULL);
	assert_true(authenticated_at_entry);
	assert_int_equal(validations, 1);
	assert_int_equal(signatures, 0);
	assert_int_equal(pending_slot, 0);
}

static void hmac_timeout(void **state)
{
	timeout = true;
	expect_assert_failure(prepare(NULL));
	assert_int_equal(pending_slot, 0);
	assert_false(authenticated_at_entry);
}

static void unknown_partition(void **state)
{
	current = -1;
	prepare(NULL);
	assert_int_equal(pending_slot, 0);
	current = 0;
	unknown_ancestry = true;
	prepare(NULL);
	assert_false(authenticated_at_entry);
	assert_int_equal(pending_slot, 0);
	assert_int_equal(signatures, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		{ "slot_a", selected_slot, setup, NULL, (void *)0 },
		{ "slot_b", selected_slot, setup, NULL, (void *)1 },
		cmocka_unit_test_setup(existing_macs, setup),
		cmocka_unit_test_setup(partition_changed, setup),
		cmocka_unit_test_setup(unknown_partition, setup),
		{ "invalid_active_a", invalid_existing_mac, setup, NULL, (void *)0 },
		{ "invalid_active_b", invalid_existing_mac, setup, NULL, (void *)1 },
		{ "invalid_inactive_b", invalid_inactive_bank, setup, NULL, (void *)0 },
		{ "invalid_inactive_a", invalid_inactive_bank, setup, NULL, (void *)1 },
		cmocka_unit_test_setup(hmac_timeout, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
