/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Exercise the same private timestamp formatter linked into cbmem. */
int cbmem_main(int argc, char **argv);
#define main cbmem_main
#include "cbmem.c"
#undef main

int main(void)
{
	static const struct {
		uint32_t first;
		uint32_t last;
	} allocated_ranges[] = {
		{ 0x1800, 0x1803 }, { 0x1820, 0x182e }, { 0x1830, 0x1838 },
		{ 0x1840, 0x1851 }, { 0x1860, 0x1875 }, { 0x1880, 0x1880 },
		{ 0x18a0, 0x18a2 }, { 0x1900, 0x1933 }, { 0x1960, 0x1983 },
		{ 0x19a0, 0x19a2 }, { 0x19c0, 0x19c3 }, { 0x1a00, 0x1a20 },
		{ 0x1a40, 0x1a53 }, { 0x1a60, 0x1a68 },
	};
	static const uint32_t holes[] = {
		0x1002, 0x130a, 0x1328, 0x132f, 0x133c, 0x133f, 0x1343,
		0x1355, 0x15ff, 0x1620, 0x1621, 0x1626, 0x1649, 0x164c,
		0x1680, 0x1700, 0x1707, 0x17df, 0x17ea, 0x17eb, 0x17ec,
		0x17ed, 0x17ee, 0x17ef, 0x17fa, 0x17fb, 0x17fc, 0x17fd,
		0x17fe, 0x17ff, 0x1804, 0x181f, 0x182f,
		0x1839, 0x183f, 0x1852, 0x185f, 0x1876, 0x187f, 0x1881,
		0x189f, 0x18a3, 0x18ff, 0x1934, 0x195f, 0x1984, 0x199f,
		0x19a3, 0x19bf, 0x19c4, 0x19ff, 0x1a21, 0x1a3f, 0x1a54,
		0x1a5f, 0x1a69, 0x1aff, 0x1b00, 0x1b56,
	};
	static const uint32_t allocated_ids[] = {
		0x1000, 0x1001, 0x1003,
		0x1100, 0x1101, 0x1102, 0x1103,
		0x1200, 0x1201, 0x1202, 0x1203,
		0x1400, 0x1401, 0x1402,
		0x164a, 0x164b, 0x16fe,
		0x1701, 0x1702, 0x1703, 0x1704, 0x1705, 0x1706,
		0x17e0, 0x17e1, 0x17e2, 0x17e3, 0x17e4, 0x17e5,
		0x17e6, 0x17e7, 0x17e8, 0x17e9,
		0x17f0, 0x17f1, 0x17f2, 0x17f3, 0x17f4, 0x17f5,
		0x17f6, 0x17f7, 0x17f8, 0x17f9,
	};
	/* Unique CDK2 IDs in all three PR331 runs-tsc CBMEM dumps. */
	static const uint32_t observed_pr331_ids[] = {
		0x1000, 0x1001, 0x1003, 0x1100, 0x1101,
		0x1600, 0x1601, 0x1602, 0x1603, 0x1604, 0x1605, 0x1606,
		0x16fe, 0x1821, 0x1823, 0x1824, 0x1829, 0x182b, 0x182d,
		0x1835, 0x1845, 0x1846, 0x186c, 0x186e, 0x186f, 0x18a0,
		0x1900, 0x1901, 0x1902, 0x1903, 0x1904, 0x1905, 0x1906,
		0x1907, 0x1909, 0x190b, 0x1916, 0x1917, 0x1918, 0x1926,
		0x1927, 0x1932, 0x1933, 0x1971, 0x19c3, 0x1a61, 0x1a62,
		0x1a63,
	};
	static const uint32_t linear_phases[] = {
		0x1600, 0x1602, 0x1604, 0x1606, 0x1608, 0x160a,
		0x160c, 0x160e, 0x1610, 0x1612, 0x1614, 0x1616,
		0x1618, 0x161a, 0x161c, 0x161e, 0x1622, 0x1624,
	};

	for (size_t range = 0; range < ARRAY_SIZE(allocated_ranges); range++)
		for (uint32_t id = allocated_ranges[range].first;
		     id <= allocated_ranges[range].last; id++)
			assert(strcmp(timestamp_name(id), "<unknown>"));
	for (size_t i = 0; i < ARRAY_SIZE(holes); i++)
		assert(!strcmp(timestamp_name(holes[i]), "<unknown>"));
	for (size_t i = 0; i < ARRAY_SIZE(allocated_ids); i++)
		assert(strcmp(timestamp_name(allocated_ids[i]), "<unknown>"));
	for (size_t i = 0; i < ARRAY_SIZE(observed_pr331_ids); i++)
		assert(strcmp(timestamp_name(observed_pr331_ids[i]), "<unknown>"));
	for (size_t i = 0; i < ARRAY_SIZE(linear_phases); i++) {
		assert(strcmp(timestamp_name(linear_phases[i]), "<unknown>"));
		assert(strcmp(timestamp_name(linear_phases[i] + 1U), "<unknown>"));
	}
	for (uint32_t id = 0x1300; id <= 0x1327; id++)
		assert(id == 0x130a || strcmp(timestamp_name(id), "<unknown>"));
	assert(!strcmp(timestamp_name(0x130a), "<unknown>"));
	for (uint32_t id = 0x1330; id <= 0x133b; id++)
		assert(strcmp(timestamp_name(id), "<unknown>"));
	for (uint32_t id = 0x1340; id <= 0x1342; id++)
		assert(strcmp(timestamp_name(id), "<unknown>"));
	assert(!strcmp(timestamp_name(0x19a0), "CDK2 DXE firmware volume"));
	assert(!strcmp(timestamp_name(0x1830), "CDK2 DXE image service"));
	assert(!strcmp(timestamp_name(0x1838), "CDK2 DXE image service"));
	assert(!strcmp(timestamp_name(0x19c2), "CDK2 DXE event/TPL"));
	assert(!strcmp(timestamp_name(0x1a62), "CDK2 DXE core"));
	assert(!strcmp(timestamp_name(0x1900), "CDK2 DXE GCD"));
	assert(!strcmp(timestamp_name(0x1960), "CDK2 DXE memory"));
	assert(!strcmp(timestamp_name(0x18c0), "<unknown>"));
	assert(!strcmp(timestamp_name(0x1302), "CDK2 BDS boot begin"));
	assert(!strcmp(timestamp_name(0x1327), "CDK2 BDS boot-option diagnostic"));
	assert(!strcmp(timestamp_name(0x133b), "CDK2 BDS image diagnostic"));
	assert(!strcmp(timestamp_name(0x1342), "CDK2 BDS load-option diagnostic"));
	assert(!strcmp(timestamp_name(0x1402), "CDK2 ATA/ATAPI ready"));
	assert(!strcmp(timestamp_name(0x164b), "CDK2 USB subphase scan end"));
	assert(!strcmp(timestamp_name(0x161c), "CDK2 display adoption begin"));
	assert(!strcmp(timestamp_name(0x161d), "CDK2 display adoption complete"));
	assert(!strcmp(timestamp_name(0x16fe), "CDK2 timestamp overflow"));
	assert(!strcmp(timestamp_name(0x1706), "CDK2 disk capsule complete"));
	assert(!strcmp(timestamp_name(0x17e6), "CDK2 PCI bus discovery begin"));
	assert(!strcmp(timestamp_name(0x17e9), "CDK2 PCI bus child publication result"));
	assert(!strcmp(timestamp_name(0x17f0), "CDK2 PCI discovery root HOB invalid"));
	assert(!strcmp(timestamp_name(0x17f9),
		"CDK2 PCI discovery parent path allocation failed"));
	assert(!strcmp(timestamp_name(0x2000), "CDK2 upstream diagnostic"));
	assert(!strcmp(timestamp_name(0x2fff), "CDK2 upstream diagnostic"));
	assert(!strcmp(timestamp_name(0x3000), "<unknown>"));
	printf("PR331 live CBMEM: %zu CDK2 IDs named, 0 unknown\n",
		ARRAY_SIZE(observed_pr331_ids));
	puts("CDK2 timestamp names and sparse-range holes passed");
	return 0;
}
