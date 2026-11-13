/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#define main coreboot_main
#include <security/tcg/opal_s3/opal_s3_resources.c>
#undef main
#include <security/tcg/opal_s3/opal_s3_resource_import.c>
#include <cpu/x86/smm/pci_resource_store.c>

struct device *all_devices;
static bool resume;
static struct smm_runtime runtime;
static struct bus bus;
static struct resource bar;
static struct device nvme;

bool romstage_handoff_is_resume(void)
{
	return resume;
}

bool smm_region_overlaps_handler(const struct region *region)
{
	return false;
}

bool is_pci(const struct device *dev)
{
	return dev->path.type == DEVICE_PATH_PCI;
}

const char *dev_path(const struct device *dev)
{
	return "test NVMe";
}

static int setup(void **unused)
{
	memset(&runtime, 0, sizeof(runtime));
	memset(&pending, 0, sizeof(pending));
	memset(&imported, 0, sizeof(imported));
	consumed = false;
	resume = false;
	all_devices = NULL;
	bus = (struct bus) {.secondary = 2};
	bar = (struct resource) {
		.index = PCI_BASE_ADDRESS_0, .base = 0xe0000000, .size = 0x4000,
		.flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED,
	};
	nvme = (struct device) {
		.path = {.type = DEVICE_PATH_PCI, .pci.devfn = 0}, .upstream = &bus,
		.enabled = 1, .vendor = 0x144d, .device = 0xa80d,
		.class = OPAL_NVME_CLASS, .resource_list = &bar,
	};
	return 0;
}

static void late_capture(void **unused)
{
	/* SMM loads before enumeration, with another user's slot already populated. */
	runtime.pci_resources[0].vendor_id = 0x8086;
	runtime.pci_resources[0].pci_addr = PCI_DEV(0, 20, 0);
	runtime.pci_resources[0].resources[0].next =
		&runtime.pci_resources[0].resources[1];
	runtime.pci_resources[0].resources[1].base = 0xd0000000;
	smm_pci_resource_store_init(&runtime);
	assert_false(pending.ready);
	all_devices = &nvme;
	store_pci_resources(NULL);
	assert_true(pending.ready);
	pending.slots[0].resources[0].next = &pending.slots[0].resources[1];
	assert_int_equal(opal_s3_import_pci_resources(&runtime), 0);
	assert_ptr_equal(imported.slots[0].resources[0].next,
			 &imported.slots[0].resources[1]);
	assert_null(imported.slots[0].resources[1].next);
	assert_int_equal(imported.slots[0].resources[1].base, 0xd0000000);
	assert_int_equal(imported.slots[0].vendor_id, 0x8086);
	assert_int_equal(imported.slots[1].vendor_id, 0x144d);
	assert_int_equal(imported.slots[1].pci_addr, PCI_DEV(2, 0, 0));
	assert_int_equal(imported.slots[1].resources[0].base, bar.base);
	memset(&pending, 0, sizeof(pending));
	assert_int_equal(opal_s3_import_pci_resources(&runtime), -1);
	assert_int_equal(imported.slots[1].vendor_id, 0x144d);
}

static void empty_and_unready(void **unused)
{
	smm_pci_resource_store_init(&runtime);
	assert_int_equal(opal_s3_import_pci_resources(&runtime), -1);
	assert_true(consumed);
	assert_null(opal_s3_get_pci_resources());
	consumed = false;
	smm_pci_resource_store_init(&runtime);
	store_pci_resources(NULL);
	assert_true(pending.ready);
	assert_int_equal(opal_s3_import_pci_resources(&runtime), 0);
	assert_non_null(opal_s3_get_pci_resources());
	assert_int_equal(opal_s3_get_pci_resources()[0].vendor_id, 0);
}

static void full_store(void **unused)
{
	for (size_t i = 0; i < ARRAY_SIZE(runtime.pci_resources); i++) {
		runtime.pci_resources[i].vendor_id = 0x8086;
		runtime.pci_resources[i].pci_addr = PCI_DEV(0, i, 0);
	}
	smm_pci_resource_store_init(&runtime);
	all_devices = &nvme;
	store_pci_resources(NULL);
	assert_false(pending.ready);
	assert_int_equal(opal_s3_import_pci_resources(&runtime), -1);
	assert_int_equal(runtime.pci_resources[0].vendor_id, 0x8086);
	assert_null(opal_s3_get_pci_resources());
}

static void resume_does_not_import(void **unused)
{
	resume = true;
	runtime.s3_resume = true;
	smm_pci_resource_store_init(&runtime);
	all_devices = &nvme;
	store_pci_resources(NULL);
	assert_false(pending.ready);
	/* Even an OS-modified staging table must not repopulate the SMM list. */
	pending.ready = true;
	pending.slots[0].vendor_id = 0x144d;
	assert_int_equal(opal_s3_import_pci_resources(&runtime), 0);
	assert_true(consumed);
	assert_null(opal_s3_get_pci_resources());
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(late_capture, setup),
		cmocka_unit_test_setup(empty_and_unready, setup),
		cmocka_unit_test_setup(full_store, setup),
		cmocka_unit_test_setup(resume_does_not_import, setup),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
