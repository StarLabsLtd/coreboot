/* SPDX-License-Identifier: GPL-2.0-only */
#include <boot/coreboot_tables.h>
#include <crc_byte.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <tests/test.h>

const unsigned int coreboot_version_timestamp = 0x12345678;
static uint8_t storage[2048];
static struct bus buses[2];
static struct device devs[5];
static struct resource res[8];
struct device *all_devices;

const char *dev_path(const struct device *dev) { return "fixture"; }
struct lb_record *lb_new_record(struct lb_header *h)
{
	struct lb_record *r = (void *)((uint8_t *)h + sizeof(*h) + h->table_bytes);
	if (h->table_entries) {
		h->table_bytes += r->size;
		r = (void *)((uint8_t *)h + sizeof(*h) + h->table_bytes);
	}
	h->table_entries++;
	r->size = sizeof(*r);
	return r;
}

static void set_res(int i, uint64_t base, uint64_t size, unsigned long flags,
		    unsigned long index, struct resource *next)
{
	res[i] = (struct resource) {
		.base = base, .size = size, .flags = flags, .index = index, .next = next,
	};
}

static int setup(void **state)
{
	memset(storage, 0, sizeof(storage));
	memset(buses, 0, sizeof(buses));
	memset(devs, 0, sizeof(devs));
	memset(res, 0, sizeof(res));
	devs[0].enabled = 1;
	devs[0].path.type = DEVICE_PATH_DOMAIN;
	devs[0].downstream = &buses[0];
	devs[0].next = &devs[1];
	buses[0].dev = &devs[0];
	buses[0].subordinate = 0x7f;
	devs[1].enabled = 1;
	devs[1].path.type = DEVICE_PATH_PCI;
	devs[1].path.pci.devfn = PCI_DEVFN(2, 0);
	devs[1].upstream = &buses[0];
	devs[1].resource_list = &res[0];
	devs[1].next = &devs[2];
	set_res(0, 0x10000000, 0x1000, IORESOURCE_MEM | IORESOURCE_ASSIGNED, 0x10, &res[1]);
	set_res(1, 0x10010000, 0x2000,
		IORESOURCE_MEM | IORESOURCE_PREFETCH | IORESOURCE_ASSIGNED, 0x18, &res[2]);
	set_res(2, 0x100000000ULL, 0x4000,
		IORESOURCE_MEM | IORESOURCE_PREFETCH | IORESOURCE_PCI64 |
		IORESOURCE_ASSIGNED, 0x20, NULL);
	set_res(3, 0x2000, 0x20, IORESOURCE_IO | IORESOURCE_ASSIGNED, 0x24, NULL);
	devs[2].enabled = 1;
	devs[2].path.type = DEVICE_PATH_PCI;
	devs[2].path.pci.devfn = PCI_DEVFN(3, 0);
	devs[2].upstream = &buses[0];
	devs[2].resource_list = &res[3];
	res[3].next = &res[4];
	set_res(4, 0, 0xa0000, IORESOURCE_MEM | IORESOURCE_CACHEABLE |
		IORESOURCE_ASSIGNED | IORESOURCE_FIXED, 0xf, NULL);
	all_devices = &devs[0];
	*state = storage;
	return 0;
}

static const struct lb_payload_resource_handoff *get_handoff(void *state)
{
	return (const void *)((uint8_t *)state + sizeof(struct lb_header));
}

static uint32_t crc(const struct lb_payload_resource_handoff *h)
{
	const uint8_t *p = (const void *)h;
	const size_t off = offsetof(struct lb_payload_resource_handoff, crc32);
	uint32_t value = 0;
	for (size_t i = 0; i < h->size; i++)
		value = crc32_byte(value, i >= off && i < off + sizeof(h->crc32) ? 0 : p[i]);
	return value;
}

static void test_exact_holes_and_prefetch(void **state)
{
	const struct lb_payload_resource_handoff *h;
	const struct lb_payload_resource_section *s;
	const struct lb_prh_pci_root_bridge *root;
	const struct lb_prh_pci_assignment *a;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_SUCCESS);
	h = get_handoff(*state);
	assert_int_equal(h->section_count, 2);
	assert_int_equal(crc(h), h->crc32);
	s = h->sections;
	assert_int_equal(s[0].entry_count, 1);
	root = (const void *)((const uint8_t *)h + s[0].offset);
	assert_int_equal(root->flags, LB_PRH_PCI_ROOT_TOPOLOGY_ONLY);
	assert_int_equal(root->mem32_length, 0);
	assert_int_equal(s[1].type, LB_PRH_SECTION_PCI_ASSIGNMENTS);
	assert_int_equal(s[1].entry_count, 4);
	a = (const void *)((const uint8_t *)h + s[1].offset);
	assert_int_equal(a[0].base, 0x10000000);
	assert_int_equal(a[0].length, 0x1000);
	assert_int_equal(a[0].resource_type, LB_PRH_PCI_RESOURCE_MMIO32);
	assert_int_equal(a[1].base, 0x10010000);
	assert_int_equal(a[1].resource_type, LB_PRH_PCI_RESOURCE_PREFETCH_MMIO32);
	assert_true(a[0].base + a[0].length < a[1].base);
	assert_int_equal(a[2].resource_type, LB_PRH_PCI_RESOURCE_PREFETCH_MMIO64);
	assert_int_equal(a[2].flags, LB_PRH_PCI_ASSIGNMENT_64BIT);
}

static void test_duplicate(void **state)
{
	set_res(5, res[0].base, res[0].size, IORESOURCE_MEM | IORESOURCE_ASSIGNED, 0x14, NULL);
	res[4].next = &res[5];
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
	assert_int_equal(((struct lb_header *)*state)->table_entries, 0);
}

static void test_duplicate_resource_identity(void **state)
{
	set_res(5, 0x20000000, 0x1000, IORESOURCE_MEM | IORESOURCE_ASSIGNED, 0x10, NULL);
	res[3].next = &res[5];
	devs[2].path.pci.devfn = devs[1].path.pci.devfn;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_rejects_64bit_bar5(void **state)
{
	res[2].index = PCI_BASE_ADDRESS_5;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_rejects_64bit_high_half_assignment(void **state)
{
	set_res(5, 0x200000000ULL, 0x1000,
		IORESOURCE_MEM | IORESOURCE_ASSIGNED, PCI_BASE_ADDRESS_5, NULL);
	res[2].next = &res[5];
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_rejects_cross_type_same_bar(void **state)
{
	set_res(5, 0x3000, 0x20, IORESOURCE_IO | IORESOURCE_ASSIGNED,
		PCI_BASE_ADDRESS_0, NULL);
	res[2].next = &res[5];
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_rejects_io_in_64bit_high_half(void **state)
{
	set_res(5, 0x3000, 0x20, IORESOURCE_IO | IORESOURCE_ASSIGNED,
		PCI_BASE_ADDRESS_5, NULL);
	res[2].next = &res[5];
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_rejects_above4g_non_pci64(void **state)
{
	res[2].flags &= ~IORESOURCE_PCI64;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_rejects_pci64_io(void **state)
{
	res[3].index = PCI_BASE_ADDRESS_0;
	res[3].flags |= IORESOURCE_PCI64;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_rejects_devfn_narrowing(void **state)
{
	devs[1].path.pci.devfn = 0x100;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_rejects_high_resource_index(void **state)
{
	res[0].index = 0x344;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_rejects_narrowing_collision(void **state)
{
	res[0].index = 0x110;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_reserved(void **state)
{
	res[0].base = 0x1000;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_ecam(void **state)
{
	res[0].base = CONFIG_ECAM_MMCONF_BASE_ADDRESS;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void add_second_root(void)
{
	devs[2].next = &devs[3];
	devs[3].enabled = 1;
	devs[3].path.type = DEVICE_PATH_DOMAIN;
	devs[3].downstream = &buses[1];
	devs[3].next = &devs[4];
	buses[1].dev = &devs[3];
	buses[1].secondary = 0x80;
	buses[1].subordinate = 0xff;
	devs[4].enabled = 1;
	devs[4].path.type = DEVICE_PATH_PCI;
	devs[4].path.pci.devfn = PCI_DEVFN(1, 0);
	devs[4].upstream = &buses[1];
	devs[4].resource_list = &res[5];
	set_res(5, 0x20000000, 0x1000, IORESOURCE_MEM | IORESOURCE_ASSIGNED, 0x10, NULL);
}

static void test_two_roots_same_segment(void **state)
{
	const struct lb_payload_resource_handoff *h;
	const struct lb_prh_pci_root_bridge *roots;
	add_second_root();
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_SUCCESS);
	h = get_handoff(*state);
	assert_int_equal(h->sections[0].entry_count, 2);
	roots = (const void *)((const uint8_t *)h + h->sections[0].offset);
	assert_int_equal(roots[0].bus_start, 0);
	assert_int_equal(roots[1].bus_start, 0x80);
}

static void test_overlapping_roots(void **state)
{
	add_second_root();
	buses[1].secondary = 0x70;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_orphan(void **state)
{
	devs[1].upstream = NULL;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

static void test_outside_mcfg(void **state)
{
	buses[0].subordinate = CONFIG_ECAM_MMCONF_BUS_NUMBER;
	assert_int_equal(lb_add_payload_resource_handoff(*state), CB_ERR);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_exact_holes_and_prefetch, setup),
		cmocka_unit_test_setup(test_duplicate, setup),
		cmocka_unit_test_setup(test_duplicate_resource_identity, setup),
		cmocka_unit_test_setup(test_rejects_64bit_bar5, setup),
		cmocka_unit_test_setup(test_rejects_64bit_high_half_assignment, setup),
		cmocka_unit_test_setup(test_rejects_cross_type_same_bar, setup),
		cmocka_unit_test_setup(test_rejects_io_in_64bit_high_half, setup),
		cmocka_unit_test_setup(test_rejects_above4g_non_pci64, setup),
		cmocka_unit_test_setup(test_rejects_pci64_io, setup),
		cmocka_unit_test_setup(test_rejects_devfn_narrowing, setup),
		cmocka_unit_test_setup(test_rejects_high_resource_index, setup),
		cmocka_unit_test_setup(test_rejects_narrowing_collision, setup),
		cmocka_unit_test_setup(test_reserved, setup),
		cmocka_unit_test_setup(test_ecam, setup),
		cmocka_unit_test_setup(test_two_roots_same_segment, setup),
		cmocka_unit_test_setup(test_overlapping_roots, setup),
		cmocka_unit_test_setup(test_orphan, setup),
		cmocka_unit_test_setup(test_outside_mcfg, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
