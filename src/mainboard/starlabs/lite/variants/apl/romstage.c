/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>
#include <soc/romstage.h>
#include <fsp/api.h>
#include <FspmUpd.h>

static const uint8_t Ch0_Bit_swizzling[] = {
	0x11, 0x16, 0x13, 0x14, 0x15, 0x10, 0x12, 0x17,
	0x08, 0x0A, 0x0F, 0x0C, 0x09, 0x0D, 0x0B, 0x0E,
	0x01, 0x06, 0x07, 0x05, 0x00, 0x03, 0x02, 0x04,
	0x18, 0x1F, 0x19, 0x1D, 0x1E, 0x1B, 0x1A, 0x1C
};
static const uint8_t Ch1_Bit_swizzling[] = {
	0x05, 0x06, 0x01, 0x00, 0x02, 0x04, 0x03, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0D, 0x0C, 0x0E, 0x0F,
	0x10, 0x14, 0x17, 0x12, 0x16, 0x13, 0x11, 0x15,
	0x1A, 0x18, 0x1F, 0x1B, 0x1C, 0x1D, 0x1E, 0x19
};
static const uint8_t Ch2_Bit_swizzling[] = {
	0x00, 0x0E, 0x0F, 0x09, 0x0A, 0x0B, 0x0C, 0x08,
	0x16, 0x15, 0x17, 0x11, 0x10, 0x14, 0x13, 0x12,
	0x02, 0x07, 0x05, 0x01, 0x06, 0x04, 0x00, 0x03,
	0x1F, 0x1A, 0x1B, 0x1D, 0x18, 0x19, 0x1C, 0x1E
};
static const uint8_t Ch3_Bit_swizzling[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x06, 0x05, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x12, 0x11, 0x14, 0x16, 0x13, 0x15, 0x10, 0x17,
	0x18, 0x1A, 0x1D, 0x1C, 0x1B, 0x1F, 0x1E, 0x19
};

void mainboard_memory_init_params(FSPM_UPD *memupd)
{
	memupd->FspmConfig.Package			= 0x01,
	memupd->FspmConfig.Profile			= 0x05,
	memupd->FspmConfig.MemoryDown			= 0x01,
	memupd->FspmConfig.DDR3LPageSize		= 0x01,
	memupd->FspmConfig.DDR3LASR			= 0x00,
	memupd->FspmConfig.ScramblerSupport		= 0x01,
	memupd->FspmConfig.ChannelHashMask		= 0x00,
	memupd->FspmConfig.SliceHashMask		= 0x00,
	memupd->FspmConfig.InterleavedMode		= 0x00,
	memupd->FspmConfig.ChannelsSlicesEnable		= 0x00,
	memupd->FspmConfig.MinRefRate2xEnable		= 0x00,
	memupd->FspmConfig.DualRankSupportEnable	= 0x00,
	memupd->FspmConfig.RmtMode			= 0x00,
	memupd->FspmConfig.MemorySizeLimit		= 0x00,
	memupd->FspmConfig.LowMemoryMaxValue		= 0x00,
	memupd->FspmConfig.DisableFastBoot		= 0x00,
	memupd->FspmConfig.HighMemoryMaxValue		= 0x00,
	memupd->FspmConfig.DIMM0SPDAddress		= 0x00,
	memupd->FspmConfig.DIMM1SPDAddress		= 0x00,

	memupd->FspmConfig.Ch0_RankEnable		= 0x03,
	memupd->FspmConfig.Ch0_DeviceWidth		= 0x02,
	memupd->FspmConfig.Ch0_DramDensity		= 0x02,
	memupd->FspmConfig.Ch0_Option			= 0x03,
	memupd->FspmConfig.Ch0_OdtConfig		= 0x00,
	memupd->FspmConfig.Ch0_TristateClk1		= 0x00,
	memupd->FspmConfig.Ch0_Mode2N			= 0x00,
	memupd->FspmConfig.Ch0_OdtLevels		= 0x00,

	memupd->FspmConfig.Ch1_RankEnable		= 0x03,
	memupd->FspmConfig.Ch1_DeviceWidth		= 0x02,
	memupd->FspmConfig.Ch1_DramDensity		= 0x02,
	memupd->FspmConfig.Ch1_Option			= 0x03,
	memupd->FspmConfig.Ch1_OdtConfig		= 0x00,
	memupd->FspmConfig.Ch1_TristateClk1		= 0x00,
	memupd->FspmConfig.Ch1_Mode2N			= 0x00,
	memupd->FspmConfig.Ch1_OdtLevels		= 0x00,

	memupd->FspmConfig.Ch2_RankEnable		= 0x03,
	memupd->FspmConfig.Ch2_DeviceWidth		= 0x02,
	memupd->FspmConfig.Ch2_DramDensity		= 0x02,
	memupd->FspmConfig.Ch2_Option			= 0x03,
	memupd->FspmConfig.Ch2_OdtConfig		= 0x00,
	memupd->FspmConfig.Ch2_TristateClk1		= 0x00,
	memupd->FspmConfig.Ch2_Mode2N			= 0x00,
	memupd->FspmConfig.Ch2_OdtLevels		= 0x00,

	memupd->FspmConfig.Ch3_RankEnable		= 0x03,
	memupd->FspmConfig.Ch3_DeviceWidth		= 0x02,
	memupd->FspmConfig.Ch3_DramDensity		= 0x02,
	memupd->FspmConfig.Ch3_Option			= 0x03,
	memupd->FspmConfig.Ch3_OdtConfig		= 0x00,
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
