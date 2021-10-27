/* SPDX-License-Identifier: GPL-2.0-only */

#include <gpio.h>
#include <soc/meminit.h>
#include <soc/romstage.h>
#include <string.h>

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

void mainboard_memory_init_params(FSPM_UPD *memupd)
{
	memupd->FspmConfig.Package			= 0x01,
	memupd->FspmConfig.Profile			= 0x0B,
	memupd->FspmConfig.MemoryDown			= 0x01,
	memupd->FspmConfig.DDR3LPageSize		= 0x00,
	memupd->FspmConfig.DDR3LASR			= 0x00,
	memupd->FspmConfig.ScramblerSupport		= 0x01,
	memupd->FspmConfig.ChannelHashMask		= 0x36,
	memupd->FspmConfig.SliceHashMask		= 0x09,
	memupd->FspmConfig.InterleavedMode		= 0x02,
	memupd->FspmConfig.ChannelsSlicesEnable		= 0x00,
	memupd->FspmConfig.MinRefRate2xEnable		= 0x00,
	memupd->FspmConfig.DualRankSupportEnable	= 0x01,
	memupd->FspmConfig.RmtMode			= 0x00,
	memupd->FspmConfig.MemorySizeLimit		= 0x00,
	memupd->FspmConfig.LowMemoryMaxValue		= 0x00,
	memupd->FspmConfig.DisableFastBoot		= 0x00,
	memupd->FspmConfig.HighMemoryMaxValue		= 0x00,
	memupd->FspmConfig.DIMM0SPDAddress		= 0x00,
	memupd->FspmConfig.DIMM1SPDAddress		= 0x00,

	memupd->FspmConfig.Ch0_RankEnable		= 0x03,
	memupd->FspmConfig.Ch0_DeviceWidth		= 0x01,
	memupd->FspmConfig.Ch0_DramDensity		= 0x02,
	memupd->FspmConfig.Ch0_Option			= 0x03,
	memupd->FspmConfig.Ch0_OdtConfig		= 0x02,
	memupd->FspmConfig.Ch0_TristateClk1		= 0x00,
	memupd->FspmConfig.Ch0_Mode2N			= 0x00,
	memupd->FspmConfig.Ch0_OdtLevels		= 0x00,

	memupd->FspmConfig.Ch1_RankEnable		= 0x03,
	memupd->FspmConfig.Ch1_DeviceWidth		= 0x01,
	memupd->FspmConfig.Ch1_DramDensity		= 0x02,
	memupd->FspmConfig.Ch1_Option			= 0x03,
	memupd->FspmConfig.Ch1_OdtConfig		= 0x02,
	memupd->FspmConfig.Ch1_TristateClk1		= 0x00,
	memupd->FspmConfig.Ch1_Mode2N			= 0x00,
	memupd->FspmConfig.Ch1_OdtLevels		= 0x00,

	memupd->FspmConfig.Ch2_RankEnable		= 0x03,
	memupd->FspmConfig.Ch2_DeviceWidth		= 0x01,
	memupd->FspmConfig.Ch2_DramDensity		= 0x02,
	memupd->FspmConfig.Ch2_Option			= 0x03,
	memupd->FspmConfig.Ch2_OdtConfig		= 0x02,
	memupd->FspmConfig.Ch2_TristateClk1		= 0x00,
	memupd->FspmConfig.Ch2_Mode2N			= 0x00,
	memupd->FspmConfig.Ch2_OdtLevels		= 0x00,

	memupd->FspmConfig.Ch3_RankEnable		= 0x03,
	memupd->FspmConfig.Ch3_DeviceWidth		= 0x01,
	memupd->FspmConfig.Ch3_DramDensity		= 0x02,
	memupd->FspmConfig.Ch3_Option			= 0x03,
	memupd->FspmConfig.Ch3_OdtConfig		= 0x02,
	memupd->FspmConfig.Ch3_TristateClk1		= 0x00,
	memupd->FspmConfig.Ch3_Mode2N			= 0x00,
	memupd->FspmConfig.Ch3_OdtLevels		= 0x00,

	memupd->FspmConfig.RmtCheckRun			= 0x00,

	memcpy(memupd->FspmConfig.Ch0_Bit_swizzling, &Ch0_Bit_swizzling,
		sizeof(Ch0_Bit_swizzling));
	memcpy(memupd->FspmConfig.Ch1_Bit_swizzling, &Ch1_Bit_swizzling,
		sizeof(Ch1_Bit_swizzling));
	memcpy(memupd->FspmConfig.Ch2_Bit_swizzling, &Ch2_Bit_swizzling,
		sizeof(Ch2_Bit_swizzling));
	memcpy(memupd->FspmConfig.Ch3_Bit_swizzling, &Ch3_Bit_swizzling,
		sizeof(Ch3_Bit_swizzling));

	memupd->FspmConfig.RmtMarginCheckScaleHighThreshold	= 0x0;
	memupd->FspmConfig.MsgLevelMask				= 0x0;
}
