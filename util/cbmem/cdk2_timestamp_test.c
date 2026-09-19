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
	static const char *const names[] = {
		"SecurityStubDxe", "PcdDxe", "Tcg2Dxe", "CpuDxe",
		"RuntimeDxe", "CpuIo2Dxe", "Metronome",
		"ResetSystemRuntimeDxe", "LocalApicTimerDxe",
		"WatchdogTimer", "SmmAccessDxe", "SmmControlRuntimeDxe",
		"PiSmmIpl", "SmmStoreFvbRuntimeDxe",
		"FaultTolerantWriteDxe", "VariableRuntimeDxe", "PcRtc",
		"MonotonicCounterRuntimeDxe", "AcpiTableDxe",
		"PciHostBridgeDxe", "PciBusDxe", "NvmExpressDxe",
		"SataController", "AtaAtapiPassThruDxe", "AtaBusDxe",
		"XhciDxe", "UsbBusDxe", "UsbMassStorageDxe", "DiskIoDxe",
		"PartitionDxe", "EnglishDxe", "Fat", "CapsuleRuntimeDxe",
		"QemuTestFmpDxe", "EsrtDxe", "ConSplitterDxe",
		"UsbKbDxe", "LvglUiDxe", "BdsDxe",
		"EcAcpiBatteryStatusDxe", "DevicePathDxe", "ScsiBusDxe",
		"ScsiDiskDxe",
	};
	char expected[80];

	assert(ARRAY_SIZE(names) == 43);
	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		snprintf(expected, sizeof(expected), "CDK2 %s dispatch begin", names[i]);
		assert(!strcmp(timestamp_name(0x1b00 + 2 * i), expected));
		snprintf(expected, sizeof(expected), "CDK2 %s dispatch returned", names[i]);
		assert(!strcmp(timestamp_name(0x1b01 + 2 * i), expected));
	}
	assert(!strcmp(timestamp_name(0x1aff), "<unknown>"));
	assert(!strcmp(timestamp_name(0x1b56), "<unknown>"));
	assert(!strcmp(timestamp_name(0x19a0), "CDK2 DXE firmware volume"));
	assert(!strcmp(timestamp_name(0x1834), "CDK2 DXE image"));
	assert(!strcmp(timestamp_name(0x1838), "CDK2 DXE image"));
	assert(!strcmp(timestamp_name(0x1833), "<unknown>"));
	assert(!strcmp(timestamp_name(0x19c2), "CDK2 DXE event/TPL"));
	assert(!strcmp(timestamp_name(0x1a62), "CDK2 DXE core"));
	assert(!strcmp(timestamp_name(0x1900), "CDK2 DXE GCD"));
	assert(!strcmp(timestamp_name(0x1960), "CDK2 DXE memory"));
	assert(!strcmp(timestamp_name(0x18c0), "<unknown>"));
	assert(!strcmp(timestamp_name(0x19a3), "<unknown>"));
	assert(!strcmp(timestamp_name(0x1a69), "<unknown>"));
	assert(!strcmp(timestamp_name(0x1355), "CDK2 BDS USB scan begin"));
	assert(!strcmp(timestamp_name(0x1402), "CDK2 ATA/ATAPI ready"));
	assert(!strcmp(timestamp_name(0x164b), "CDK2 USB subphase scan end"));
	assert(!strcmp(timestamp_name(0x1680), "CDK2 DXE initial free memory"));
	assert(!strcmp(timestamp_name(0x1682), "CDK2 capsule scan location"));
	assert(!strcmp(timestamp_name(0x1687), "CDK2 setup ESP selection"));
	assert(!strcmp(timestamp_name(0x1706), "CDK2 disk capsule complete"));
	assert(!strcmp(timestamp_name(0x1700), "<unknown>"));
	assert(!strcmp(timestamp_name(0x1707), "<unknown>"));
	puts("CDK2 timestamp names: 86 allocated IDs and adjacent holes passed");
	return 0;
}
