/* SPDX-License-Identifier: GPL-2.0-only */

#include <opensil_config.h>
#include <CCX/CcxClass-api.h>
#include <CCX/Common/CcxApic.h>
#include <DF/DfClass-api.h>
#include <DF/DfX/PHX/DfSilFabricInfoPhx.h>
#include <FCH/FchClass-api.h>
#include <FCH/FchHwAcpi-api.h>
#include <FCH/Common/FchCommon.h>
#include <FchUsb-api.h>
#include <Tacoma/FchCore/FchUsb/FchUsbOemTc.h>
#include <Tacoma/FchCore/FchUsb/FchUsbRegTc.h>
#include <RcMgr/DfX/RcManager-api.h>
#include <amdblocks/ioapic.h>
#include <amdblocks/reset.h>
#include <bootstate.h>
#include <cbmem.h>
#include <cpu/cpu.h>
#include <cpu/x86/smm.h>
#include <device/device.h>
#include <soc/aoac_defs.h>
#include <soc/iomap.h>
#include <soc/amd/phoenix/chip.h>
#include <string.h>
#include <static.h>
#include <stdio.h>
#include <xSIM-api.h>

#include "../opensil.h"

void SIL_STATUS_report(const char *function, const int status)
{
	const int log_level = status == SilPass ? BIOS_DEBUG : BIOS_ERR;
	const char *error_string = "Unknown error";

	const struct error_string_entry {
		SIL_STATUS status;
		const char *string;
	} errors[] = {
		{SilPass, "SilPass"},
		{SilUnsupportedHardware, "SilUnsupportedHardware"},
		{SilUnsupported, "SilUnsupported"},
		{SilInvalidParameter, "SilInvalidParameter"},
		{SilAborted, "SilAborted"},
		{SilOutOfResources, "SilOutOfResources"},
		{SilNotFound, "SilNotFound"},
		{SilOutOfBounds, "SilOutOfBounds"},
		{SilDeviceError, "SilDeviceError"},
		{SilResetRequestColdImm, "SilResetRequestColdImm"},
		{SilResetRequestColdDef, "SilResetRequestColdDef"},
		{SilResetRequestWarmImm, "SilResetRequestWarmImm"},
		{SilResetRequestWarmDef, "SilResetRequestWarmDef"},
	};

	int i;
	for (i = 0; i < ARRAY_SIZE(errors); i++) {
		if (errors[i].status == status)
			error_string = errors[i].string;
	}
	printk(log_level, "%s returned %d (%s)\n", function, status, error_string);
}

static void setup_rc_manager_default(SIL_CONTEXT *SilContext)
{
	DFX_RCMGR_INPUT_BLK *rc_mgr_input_block = SilFindStructure(SilContext, SilId_RcManager, 0);

	if (!rc_mgr_input_block) {
		printk(BIOS_ERR, "OpenSIL: RC Manager block not found\n");
		return;
	}

	/* Let openSIL distribute the resources to the different PCI roots */
	rc_mgr_input_block->SetRcBasedOnNv = false;

	/* Currently 1P is the only supported configuration */
	rc_mgr_input_block->SocketNumber = 1;
	rc_mgr_input_block->RbsPerSocket = 1; // Consumer platform, 1 RootBridge/Socket
	rc_mgr_input_block->McptEnable = true;
	rc_mgr_input_block->PciExpressBaseAddress = CONFIG_ECAM_MMCONF_BASE_ADDRESS;
	rc_mgr_input_block->BottomMmioReservedForPrimaryRb = 4ull * GiB - 32 * MiB;
	rc_mgr_input_block->MmioSizePerRbForNonPciDevice = 16 * MiB;

	rc_mgr_input_block->MmioAbove4GLimit = POWER_OF_2(MIN(48, cpu_phys_address_size()));
	rc_mgr_input_block->Above4GMmioSizePerRbForNonPciDevice = 0;
}

static FCH_TC_USB_OEM_PLATFORM_TABLE usb_oem_platform_table;

static void fill_usb2_phy(FCH_USB20_PHY *dst, const struct fch_usb2_phy *src)
{
	dst->COMPDISTUNE = src->compdistune;
	dst->PLLBTUNE = src->pllbtune;
	dst->PLLITUNE = src->pllitune;
	dst->PLLPTUNE = src->pllptune;
	dst->SQRXTUNE = src->sqrxtune;
	dst->TXFSLSTUNE = src->txfslstune;
	dst->TXPREEMPAMPTUNE = src->txpreempamptune;
	dst->TXPREEMPPULSETUNE = src->txpreemppulsetune;
	dst->TXRISETUNE = src->txrisetune;
	dst->TXVREFTUNE = src->txvreftune;
	dst->TXHSXVTUNE = src->txhsxvtune;
	dst->TXRESTUNE = src->txrestune;
}

static void fill_usb3_phy(FCH_USB3_PHY *dst, const struct fch_usb3_phy *src)
{
	dst->TX_TERM_CTRL = src->tx_term_ctrl;
	dst->RX_TERM_CTRL = src->rx_term_ctrl;
	dst->TX_VBOOST_LVL_EN = src->tx_vboost_lvl_en;
	dst->TX_VBOOST_LVL = src->tx_vboost_lvl;
}

static void init_usb_oem_table_header(FCH_TC_USB_OEM_PLATFORM_TABLE *tbl)
{
	memset(tbl, 0, sizeof(*tbl));
	tbl->Version_Major = FCH_XHCI_VERSION_MAJOR_TC;
	tbl->Version_Minor = FCH_XHCI_VERSION_MINOR_TC;
	tbl->TableLength = sizeof(*tbl);
}

static void fill_usb_oem_table(FCH_TC_USB_OEM_PLATFORM_TABLE *tbl,
			       const struct usb_phy_config *phy)
{
	size_t i;

	init_usb_oem_table_header(tbl);

	for (i = 0; i < USB2_PORT_COUNT; i++)
		fill_usb2_phy(&tbl->Usb20PhyPort[i], &phy->Usb2PhyPort[i]);
	for (i = 0; i < USB3_PORT_COUNT; i++)
		fill_usb3_phy(&tbl->Usb3PhyPort[i], &phy->Usb3PhyPort[i]);

	tbl->BatteryChargerEnable = phy->BatteryChargerEnable;
	tbl->PhyP3CpmP4Support = phy->PhyP3CpmP4Support;
	for (i = 0; i < USBC_COMBO_PHY_COUNT; i++)
		tbl->ComboPhyStaticConfig[i] = phy->ComboPhyStaticConfig[i] & 0xf;
}

static void configure_usb(SIL_CONTEXT *SilContext)
{
	const struct soc_amd_phoenix_config *cfg = config_of_soc();
	FCHUSB_INPUT_BLK *fch_usb_data = SilFindStructure(SilContext, SilId_FchUsb, 0);

	if (!fch_usb_data) {
		printk(BIOS_ERR, "OpenSIL: FCH USB block not found\n");
		return;
	}

	fch_usb_data->Xhci0Enable = is_dev_enabled(DEV_PTR(xhci_0));
	fch_usb_data->Xhci1Enable = is_dev_enabled(DEV_PTR(xhci_1));

	fch_usb_data->Usb4Host[0].InitEnable = is_dev_enabled(DEV_PTR(usb4_xhci_0));
	fch_usb_data->Usb4Host[0].HostEnable = fch_usb_data->Usb4Host[0].InitEnable;
	fch_usb_data->Usb4Host[1].InitEnable = is_dev_enabled(DEV_PTR(usb4_xhci_1));
	fch_usb_data->Usb4Host[1].HostEnable = fch_usb_data->Usb4Host[1].InitEnable;

	if (cfg->usb_phy_custom)
		fill_usb_oem_table(&usb_oem_platform_table, &cfg->usb_phy);
	else
		init_usb_oem_table_header(&usb_oem_platform_table);

	fch_usb_data->OemUsbConfigurationTable = (uint64_t)(uintptr_t)&usb_oem_platform_table;
}

static void setup_data_fabric_default(SIL_CONTEXT *SilContext)
{
	DFCLASS_INPUT_BLK *df_input_block = SilFindStructure(SilContext, SilId_DfClass, 0);

	if (!df_input_block) {
		printk(BIOS_ERR, "OpenSIL: Data Fabric block not found\n");
		return;
	}

	df_input_block->AmdPciExpressBaseAddress = CONFIG_ECAM_MMCONF_BASE_ADDRESS;
}

// HACK: OpenSil uses different definitions than FSP.
#define FCH_AOAC_ESPI FCH_AOAC_DEV_ESPI
#define FCH_AOAC_I2C0 FCH_AOAC_DEV_I2C0
#define FCH_AOAC_I2C1 FCH_AOAC_DEV_I2C1
#define FCH_AOAC_I2C2 FCH_AOAC_DEV_I2C2
#define FCH_AOAC_I2C3 FCH_AOAC_DEV_I2C3
#define FCH_AOAC_I2C4 FCH_AOAC_DEV_I2C4
#define FCH_AOAC_I2C5 FCH_AOAC_DEV_I2C5
#define FCH_AOAC_I3C0 FCH_AOAC_DEV_I3C0
#define FCH_AOAC_I3C1 FCH_AOAC_DEV_I3C1
#define FCH_AOAC_I3C2 FCH_AOAC_DEV_I3C2
#define FCH_AOAC_I3C3 FCH_AOAC_DEV_I3C3
#define FCH_AOAC_UART0 FCH_AOAC_DEV_UART0
#define FCH_AOAC_UART1 FCH_AOAC_DEV_UART1
#define FCH_AOAC_UART2 FCH_AOAC_DEV_UART2
#define FCH_AOAC_UART3 FCH_AOAC_DEV_UART3
#define FCH_AOAC_UART4 FCH_AOAC_DEV_UART4

#define FCH_DEV_ENABLE(dev, aoac_dev_id) \
	fch_data->FchRunTime.FchDeviceEnableMap |= \
		((DEV_PTR(dev))->enabled ? BIT_32(aoac_dev_id) : 0)

static void configure_fch_acpi(SIL_CONTEXT *SilContext)
{
	FCHHWACPI_INPUT_BLK *fch_hwacpi_data = SilFindStructure(SilContext, SilId_FchHwAcpi, 0);
	FCHCLASS_INPUT_BLK *fch_data = SilFindStructure(SilContext, SilId_FchClass, 0);
	struct device *smb = DEV_PTR(smbus);

	fch_data->Smbus.SmbusSsid = smb->subsystem_vendor |
				    ((uint32_t)smb->subsystem_device << 16);

	fch_data->FchBldCfg.CfgSioPmeBaseAddress = 0;
	fch_data->FchBldCfg.CfgAcpiPm1EvtBlkAddr = ACPI_PM_EVT_BLK;
	fch_data->FchBldCfg.CfgAcpiPm1CntBlkAddr = ACPI_PM1_CNT_BLK;
	fch_data->FchBldCfg.CfgAcpiPmTmrBlkAddr = ACPI_PM_TMR_BLK;
	fch_data->FchBldCfg.CfgCpuControlBlkAddr = ACPI_CSTATE_CONTROL;
	fch_data->FchBldCfg.CfgAcpiGpe0BlkAddr = ACPI_GPE0_BLK;
	fch_data->FchBldCfg.CfgSmiCmdPortAddr = APM_CNT;

	fch_data->LegacyFree = true;
	fch_data->WdtEnable = false;
	fch_data->SerialIrqEnable = true;
	fch_data->CfgIoApicIdPreDefEnable = true;
	fch_data->FchIoApicId = FCH_IOAPIC_ID;

	// OpenSIL seems to use different definitions than FSP.
	// TODO: Add decinitions expected by OpenSIL (i.e: FCH_AOAC_UART0).
	fch_data->FchRunTime.FchDeviceEnableMap = 0;
	//FCH_DEV_ENABLE(espi, FCH_AOAC_DEV_ESPI);

	FCH_DEV_ENABLE(i2c_0, FCH_AOAC_DEV_I2C0);
	FCH_DEV_ENABLE(i2c_1, FCH_AOAC_DEV_I2C1);
	FCH_DEV_ENABLE(i2c_2, FCH_AOAC_DEV_I2C2);
	FCH_DEV_ENABLE(i2c_3, FCH_AOAC_DEV_I2C3);
	//FCH_DEV_ENABLE(i2c_4, FCH_AOAC_DEV_I2C4);
	//FCH_DEV_ENABLE(i2c_5, FCH_AOAC_DEV_I2C5);

	FCH_DEV_ENABLE(i3c_0, FCH_AOAC_DEV_I3C0);
	FCH_DEV_ENABLE(i3c_1, FCH_AOAC_DEV_I3C1);
	FCH_DEV_ENABLE(i3c_2, FCH_AOAC_DEV_I3C2);
	FCH_DEV_ENABLE(i3c_3, FCH_AOAC_DEV_I3C3);

	FCH_DEV_ENABLE(uart_0, FCH_AOAC_DEV_UART0);
	FCH_DEV_ENABLE(uart_1, FCH_AOAC_DEV_UART1);
	FCH_DEV_ENABLE(uart_2, FCH_AOAC_DEV_UART2);
	FCH_DEV_ENABLE(uart_3, FCH_AOAC_DEV_UART3);
	FCH_DEV_ENABLE(uart_4, FCH_AOAC_DEV_UART4);

	fch_hwacpi_data->PwrFailShadow = (CONFIG_MAINBOARD_POWER_FAILURE_STATE == 2) ?
		3 : CONFIG_MAINBOARD_POWER_FAILURE_STATE;
}

#define xApicMode 0x01
#define x2ApicMode 0x02
#define ApicAutoMode 0xff
static void configure_ccx(SIL_CONTEXT *SilContext)
{
	CCXCLASS_DATA_BLK *ccx_data = SilFindStructure(SilContext, SilId_CcxClass, 0);

	if (CONFIG(XAPIC_ONLY) || CONFIG(X2APIC_LATE_WORKAROUND))
		ccx_data->CcxInputBlock.AmdApicMode = xApicMode;
	else if (CONFIG(X2APIC_ONLY))
		ccx_data->CcxInputBlock.AmdApicMode = x2ApicMode;
	else
		ccx_data->CcxInputBlock.AmdApicMode = ApicAutoMode;

	ccx_data->CcxInputBlock.EnableAvx512 = 1;
	//ccx_data->CcxInputBlock.EnableSvmX2AVIC = true;
	//ccx_data->CcxInputBlock.EnableSvmAVIC = true;
	ccx_data->CcxInputBlock.AmdCStateIoBaseAddress = ACPI_CSTATE_CONTROL;
}

void setup_opensil(void)
{
	const size_t mem_req = xSimQueryMemoryRequirements();
	void *buf = cbmem_add(CBMEM_ID_AMD_OPENSIL, mem_req);

	SIL_CONTEXT SilContext;
	SilContext.ApobBaseAddress = CONFIG_PSP_APOB_DRAM_ADDRESS;
	SilContext.SilMemBaseAddress = (uintptr_t)buf;

	/* We run all openSIL timepoints in the same stage so using TP1 as argument is fine. */
	const SIL_STATUS assign_mem_ret = xSimAssignMemoryTp1(&SilContext, mem_req);
	SIL_STATUS_report("xSimAssignMemory", assign_mem_ret);

	setup_rc_manager_default(&SilContext);
	setup_data_fabric_default(&SilContext);
	configure_ccx(&SilContext);
	configure_fch_acpi(&SilContext);
	configure_usb(&SilContext);
}

static void opensil_entry(SIL_TIMEPOINT timepoint, SIL_CONTEXT *SilContext)
{
	SIL_STATUS ret;
	SIL_TIMEPOINT tp = (uintptr_t)timepoint;

	switch (tp) {
	case SIL_TP1:
		ret = InitializeAMDSiTp1(SilContext);
		break;
	case SIL_TP2:
		ret = InitializeAMDSiTp2(SilContext);
		break;
	case SIL_TP3:
		ret = InitializeAMDSiTp3(SilContext);
		break;
	default:
		printk(BIOS_ERR, "Unknown openSIL timepoint\n");
		return;
	}
	char opensil_function[16];
	snprintf(opensil_function, sizeof(opensil_function), "InitializeAMDSiTp%d", tp + 1);
	SIL_STATUS_report(opensil_function, ret);
	if (ret == SilResetRequestColdImm || ret == SilResetRequestColdDef) {
		printk(BIOS_INFO, "openSIL requested a cold reset");
		do_cold_reset();
	} else if (ret == SilResetRequestWarmImm || ret == SilResetRequestWarmDef) {
		printk(BIOS_INFO, "openSIL requested a warm reset");
		do_warm_reset();
	}
}

void opensil_xSIM_timepoint_1(void)
{
	SIL_CONTEXT SilContext = {
		.ApobBaseAddress = CONFIG_PSP_APOB_DRAM_ADDRESS,
		.SilMemBaseAddress = (uintptr_t)cbmem_find(CBMEM_ID_AMD_OPENSIL)
	};

	opensil_entry(SIL_TP1, &SilContext);
}

void opensil_xSIM_timepoint_2(void)
{
	SIL_CONTEXT SilContext = {
		.ApobBaseAddress = CONFIG_PSP_APOB_DRAM_ADDRESS,
		.SilMemBaseAddress = (uintptr_t)cbmem_find(CBMEM_ID_AMD_OPENSIL)
	};

	opensil_entry(SIL_TP2, &SilContext);
}

void opensil_xSIM_timepoint_3(void)
{
	SIL_CONTEXT SilContext = {
		.ApobBaseAddress = CONFIG_PSP_APOB_DRAM_ADDRESS,
		.SilMemBaseAddress = (uintptr_t)cbmem_find(CBMEM_ID_AMD_OPENSIL)
	};

	opensil_entry(SIL_TP3, &SilContext);
}
