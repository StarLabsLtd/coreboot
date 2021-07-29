/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <gpio.h>
#include <option.h>
#include <soc/romstage.h>
#include <string.h>
#include <types.h>

#include "baseboard/memory.h"


#include <string.h>
#include <soc/meminit.h>
#include <soc/romstage.h>

/* LPDDR4_2400_24_22_22 extracted from Intel BMP */
static const uint8_t Ch0_Bit_swizzling[] = {
	0x0F, 0x0B, 0x0D, 0X0E, 0x09, 0x0C, 0x0A, 0x08,
	0x06, 0x04, 0x05, 0x07, 0x03, 0x02, 0x01, 0x00,
	0x1E, 0x19, 0x18, 0x1C, 0x1D, 0x1B, 0x1F, 0x1A,
	0x14, 0x15, 0x17, 0x10, 0x16, 0x12, 0x11, 0x13
};
static const uint8_t Ch1_Bit_swizzling[] = {
	0x03, 0x05, 0x06, 0x07, 0x01, 0x04, 0x02, 0x00,
	0x0C, 0x0D, 0X0E, 0x0B, 0x0A, 0x08, 0x09, 0X0F,
	0x10, 0x16, 0x15, 0x13, 0x14, 0x17, 0x12, 0x11,
	0x1F, 0x1E, 0x1B, 0x19, 0x18, 0x1D, 0x1C, 0x1A
};
static const uint8_t Ch2_Bit_swizzling[] = {
	0x08, 0X0D, 0x0B, 0x0E, 0x09, 0X0F, 0x0C, 0X0A,
	0x04, 0x00, 0x02, 0x06, 0x05, 0x07, 0x03, 0x01,
	0x1B, 0x1C, 0x15, 0x1D, 0x1A, 0x18, 0x19, 0x1E,
	0x17, 0x12, 0x15, 0x16, 0x13, 0x10, 0x14, 0x11
};
static const uint8_t Ch3_Bit_swizzling[] = {
	0x03, 0x07, 0x06, 0x05, 0x01, 0x04, 0x02, 0x00,
	0x0C, 0x0F, 0x0D, 0X0E, 0X0A, 0x08, 0x09, 0x0B,
	0x10, 0x11, 0x12, 0x13, 0x16, 0x14, 0x17, 0x15,
	0x1C, 0x1E, 0x1D, 0x19, 0x1F, 0x18, 0x18, 0x1A
};

/* LPDDR4_2400_24_22_22 extracted from Intel BMP */
void mainboard_memory_init_params(FSPM_UPD *memupd)
{
	/* CA Connectivity Mapping = CA Mapping 1 */
	memupd->FspmConfig.Package = 0x1;
	/* Profile = LPDDR4 2400 24 22  */
	memupd->FspmConfig.Profile = 0xB;
	/* MemoryDown = Memory Down on MB  */
	memupd->FspmConfig.MemoryDown = 0x1;
	/* DDR4PageSize = 1 KB */
	memupd->FspmConfig.DDR3LPageSize = 0x0;
	/* DDR4ASR = Not Supported */
	memupd->FspmConfig.DDR3LASR = 0x0;
	/* Scrambler Support = Enabled */
	memupd->FspmConfig.ScramblerSupport = 0x1;
	/* Channel Hash Mask = Ox0036 */
	memupd->FspmConfig.ChannelHashMask = 0x36;
	/* Slice Hash Mask = Ox0009 */
	memupd->FspmConfig.SliceHashMask = 0x9;
	/* Interleaved Mode = Enabled */
	memupd->FspmConfig.InterleavedMode = 0x2;
	/* Channels Slices Enable = Disabled */
	memupd->FspmConfig.ChannelsSlicesEnable = 0x0;
	/* MinRefRate2xEnable = Disabled */
	memupd->FspmConfig.MinRefRate2xEnable = 0x0;
	/* Dual Rank Support Enable = Enabled */
	memupd->FspmConfig.DualRankSupportEnable = 0x1;
	/* Disable FastBoot = Disabled */
	memupd->FspmConfig.DisableFastBoot = 0x0;
	/* RMT Mode = Disabled */
	memupd->FspmConfig.RmtMode = 0x0;
	/* RMT Check Run = Ox00 */

	/* RMT Margin Check Scale High Threshold = Ox0000 */

	/* Memory Size Limit = Ox0000 */
	memupd->FspmConfig.MemorySizeLimit = 0x0;
	/* Low Memory Max Value = Ox0000 */
	memupd->FspmConfig.LowMemoryMaxValue = 0x0;
	/* High Memory Max Value = Ox0000 */
	memupd->FspmConfig.HighMemoryMaxValue = 0x0;
	/* DIMM O SPD Address = 0x00 */
	memupd->FspmConfig.DIMM0SPDAddress = 0x0;
	/* DIMM 1 SPD Address = 0x00 */
	memupd->FspmConfig.DIMM1SPDAddress = 0x0;
	/* Ch0 RankEnable = Ox03 */
	memupd->FspmConfig.Ch0_RankEnable = 0x3;
	/* Ch0 DeviceWidth = x16 */
	memupd->FspmConfig.Ch0_DeviceWidth = 0x1; // x16
	/* Ch0 DramDensity = 8Gb */
	memupd->FspmConfig.Ch0_DramDensity = 0x2; // Based on Up Squared 8Gb = 0x02
	/* Cho Option = 0x03 */
	memupd->FspmConfig.Ch0_Option = 0x3;
	/* Cho Odt Config = 0x02 */
	memupd->FspmConfig.Ch0_OdtConfig = 0x2;
	/* Cho Tristate Clk1 = Ox00 */
	memupd->FspmConfig.Ch0_TristateClk1 = 0x0;
	/* Ch0 Mode2N = Ox00 */
	memupd->FspmConfig.Ch0_Mode2N = 0x0;
	/* CH0 Odt Levels 0x00 */
	memupd->FspmConfig.Ch0_OdtLevels = 0x0;
	/* Ch1 RankEnable = 0x03 */
	memupd->FspmConfig.Ch1_RankEnable = 0x3;
	/* Ch1 DeviceWidth = x16 */
	memupd->FspmConfig.Ch1_DeviceWidth = 0x1;
	/* Ch1 DramDensity = 8Gb */
	memupd->FspmConfig.Ch1_DramDensity = 0x2;
	/* Ch1 Option = Ox03 */
	memupd->FspmConfig.Ch1_Option = 0x3 ;
	/* Ch1 Odt Config = Ox02 */
	memupd->FspmConfig.Ch1_OdtConfig = 0x2;
	/* Ch1 Tristate Cik1 = Ox00 */
	memupd->FspmConfig.Ch1_TristateClk1 = 0x0;
	/* Ch1 Mode2N = Ox00 */
	memupd->FspmConfig.Ch1_Mode2N = 0x0;
	/* Ch1 Odt Levels = Ox00 */
	memupd->FspmConfig.Ch1_OdtLevels = 0x0;
	/* Ch2 RankEnable = Ox03 */
	memupd->FspmConfig.Ch2_RankEnable = 0x3;
	/* Ch2 DeviceWidth = x16 */
	memupd->FspmConfig.Ch2_DeviceWidth = 0x1;
	/* Ch2 DramDensity = 8Gb */
	memupd->FspmConfig.Ch2_DramDensity = 0x2;
	/* Ch2 Option = Ox03 */
	memupd->FspmConfig.Ch2_Option = 0x3 ;
	/* Ch2 Odt Config = Ox00 */
	memupd->FspmConfig.Ch2_OdtConfig = 0x2;
	/* Ch2 Tristate Clk1 = Ox00 */
	memupd->FspmConfig.Ch2_TristateClk1 = 0x0;
	/* Ch2 Mode2N = Ox00 */
	memupd->FspmConfig.Ch2_Mode2N = 0x0;
	/* Ch2 Odt Levels = 0x00 */
	memupd->FspmConfig.Ch2_OdtLevels = 0x0;
	/* Ch3 RankEnable = 0x03 */
	memupd->FspmConfig.Ch3_RankEnable = 0x3;
	/* Ch3 DeviceWidth = x16 */
	memupd->FspmConfig.Ch3_DeviceWidth = 0x1;
	/* Ch3 DramDensity = 8Gb */
	memupd->FspmConfig.Ch3_DramDensity = 0x2;
	/* Ch3 Option = 0x03 */
	memupd->FspmConfig.Ch3_Option = 0x3 ;
	/* Ch3 Odt Config = Ox00 */
	memupd->FspmConfig.Ch3_OdtConfig = 0x2;
	/* Ch3 Tristate Clk1 = Ox00 */
	memupd->FspmConfig.Ch3_TristateClk1 = 0x0;
	/* Ch3 Mode2N = Ox00 */
	memupd->FspmConfig.Ch3_Mode2N = 0x0;
	/* Ch3 Odt Levels = Ox00 */
	memupd->FspmConfig.Ch3_OdtLevels = 0x0;

	memcpy(memupd->FspmConfig.Ch0_Bit_swizzling, &Ch0_Bit_swizzling,
		sizeof(Ch0_Bit_swizzling));
	memcpy(memupd->FspmConfig.Ch1_Bit_swizzling, &Ch1_Bit_swizzling,
		sizeof(Ch1_Bit_swizzling));
	memcpy(memupd->FspmConfig.Ch2_Bit_swizzling, &Ch2_Bit_swizzling,
		sizeof(Ch2_Bit_swizzling));
	memcpy(memupd->FspmConfig.Ch3_Bit_swizzling, &Ch3_Bit_swizzling,
		sizeof(Ch3_Bit_swizzling));

	memupd->FspmConfig.RmtMarginCheckScaleHighThreshold = 0x0;
	memupd->FspmConfig.MsgLevelMask = 0x0;
}
