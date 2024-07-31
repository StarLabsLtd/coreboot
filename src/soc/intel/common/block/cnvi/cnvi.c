/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <acpi/acpi_device.h>
#include <acpi/acpigen.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_ids.h>

static const char *cnvi_wifi_acpi_name(const struct device *dev)
{
	return "CNVW";
}

static void cnvw_fill_ssdt(const struct device *dev)
{
	const char *scope = acpi_device_path(dev);

	acpi_device_write_pci_dev(dev);

	acpigen_write_scope(scope);
/*
 *	OperationRegion(CWAR, SystemMemory, Add(\_SB.PCI0.GPCB(), 0xa3000), 0x100)
 *	Field(CWAR, WordAcc, NoLock, Preserve) {
 *		VDID, 32,	// 0x00, VID DID
 *		    ,  1,
 *		WMSE,  1,	// MSE
 *		WBME,  1,	// BME
 *		Offset(0x10),
 *		WBR0, 64,	// BAR0
 *		Offset(0x44),
 *		    , 28,
 *		WFLR,  1,	// Function Level Reset Capable
 *		Offset(0x48),
 *		    , 15,
 *		WIFR,  1,	// Init Function Level Reset
 *		Offset(0xcc),
 *		WPMS, 32,
 *	}
 */

	/* RegionOffset stored in Local0 */
	/* Local0 = \_SB_.PCI0.GPCB() + 0xa3000 */
	acpigen_emit_byte(ADD_OP);
	acpigen_write_integer(0xa3000);
	acpigen_emit_namestring("\\_SB_.PCI0.GPCB()");
	acpigen_emit_byte(LOCAL0_OP);

	/* OperationRegion */
	acpigen_emit_ext_op(OPREGION_OP);
	/* NameString 4 chars only */
	acpigen_emit_namestring("CWAR");
	/* RegionSpace */
	acpigen_emit_byte(SYSTEMMEMORY);
	/* RegionOffset */
	acpigen_emit_byte(LOCAL0_OP);
	/* RegionLen */
	acpigen_write_integer(0x100);

	struct fieldlist fields[] = {
		FIELDLIST_OFFSET(0),
		FIELDLIST_NAMESTR("VDID", 32),
		FIELDLIST_RESERVED(1),
		FIELDLIST_NAMESTR("WMSE", 1),
		FIELDLIST_NAMESTR("WBME", 1),
		FIELDLIST_OFFSET(0x10),
		FIELDLIST_NAMESTR("WBR0", 64),
		FIELDLIST_OFFSET(0x44),
		FIELDLIST_RESERVED(28),
		FIELDLIST_NAMESTR("WFLR", 1),
		FIELDLIST_OFFSET(0x48),
		FIELDLIST_RESERVED(15),
		FIELDLIST_NAMESTR("WIFR", 1),
		FIELDLIST_OFFSET(0xcc),
		FIELDLIST_NAMESTR("WPMS", 32),
	};
	acpigen_write_field("CWAR", fields, ARRAY_SIZE(fields),
		FIELD_WORDACC | FIELD_NOLOCK | FIELD_PRESERVE);

/*
 *	Field (CWAR, ByteAcc, NoLock, Preserve)
 *	{
 *		Offset (0xcd),
 *		PMEE,    1,
 *		    ,    6,
 *		PMES,    1
 *	}
 */
	struct fieldlist fields2[] = {
		FIELDLIST_OFFSET(0xcd),
		FIELDLIST_NAMESTR("PMEE", 1),
		FIELDLIST_RESERVED(6),
		FIELDLIST_NAMESTR("PMES", 1),
	};
	acpigen_write_field("CWAR", fields2, ARRAY_SIZE(fields2),
		FIELD_BYTEACC | FIELD_NOLOCK | FIELD_PRESERVE);

/*
 *	Method (_S0W, 0, NotSerialized)  // _S0W: S0 Device Wake State
 *	{
 *		Return (0x03)
 *	}
 */
	acpigen_write_method("_S0W", 0);
	{
		acpigen_write_return_integer(ACPI_DEVICE_SLEEP_D3_HOT);
	}
	acpigen_pop_len();
/*
 *	PowerResource(WRST, 5, 0)
 *	{
 *		Method(_STA)
 *		{
 *			Return (0x01)
 *		}
 *		Method(_ON, 0)
 *		{
 *		}
 *		Method(_OFF, 0)
 *		{
 *		}
 *		Method(_RST, 0, NotSerialized)
 *		{
 *			If (WFLR == 1)
 *			{
 *				WBR0 = 0
 *				WPMS = 0
 *				WBME = 0
 *				WMSE = 0
 *				WIFR = 1
 *			}
 *		}
 *	}
 *
 *	Name(_PRR, Package() {
 *		WRST
 *	})
 */
	acpigen_write_power_res("WRST", 5, 0, NULL, 0);
	{
		acpigen_write_method("_STA", 0);
		{
			acpigen_emit_byte(RETURN_OP);
			acpigen_write_byte(acpi_device_status(dev));
		}
		acpigen_pop_len();

		acpigen_write_method("_ON", 0);
		acpigen_pop_len();

		acpigen_write_method("_OFF", 0);
		acpigen_pop_len();

		acpigen_write_method("_RST", 0);
		{
			acpigen_write_if_lequal_namestr_int("WLFR", 1);
			{
				acpigen_write_store_int_to_namestr(0, "WBR0");
				acpigen_write_store_int_to_namestr(0, "WPMS");
				acpigen_write_store_int_to_namestr(0, "WBME");
				acpigen_write_store_int_to_namestr(0, "WMSE");
				acpigen_write_store_int_to_namestr(1, "WIFR");
			}
			acpigen_pop_len();
		}
		acpigen_pop_len();
	}
	acpigen_write_power_res_end();

	acpigen_write_name_string("_PRR", "WRST");

/*
 *	Method (GPEH, 0, NotSerialized)
 *	{
 *		If ((VDID == 0xFFFFFFFF))
 *		{
 *			Return (Zero)
 *		}
 *		If ((PMES == One))
 *		{
 *			Notify (CNVW, 0x02) // Device Wake
 *		}
 *	}
*/
	acpigen_write_method("GPEH", 0);
	{
		acpigen_write_if_lequal_namestr_int("VDID", 0xffffffff);
		{
			acpigen_write_return_integer(0);
		}
		acpigen_pop_len();

		acpigen_write_if_lequal_namestr_int("PMES", 1);
		{
			acpigen_notify("CNCW", 2);
		}
		acpigen_pop_len();
	}
	acpigen_pop_len();

/*
 *	Method (_PS0, 0, Serialized)  // _PS0: Power State 0
 *	{
 *	}
 *
 *	Method (_PS3, 0, Serialized)  // _PS3: Power State 3
 *	{
 *	}
 *
 *	Method (_DSW, 3, NotSerialized)  // _DSW: Device Sleep Wake
 *	{
 *	}
*/
	acpigen_write_method("_PS0", 0);
	acpigen_pop_len();

	acpigen_write_method("_PS3", 0);
	acpigen_pop_len();

	acpigen_write_method("_DSW", 3);
	acpigen_pop_len();

	acpigen_write_scope_end();

/*
 *	Method (CFLR, 0, NotSerialized)
 *	{
 *		If (^CNVW.WFLR == One)
 *		{
 *			^CNVW.WIFR = One
 *		}
 *	}
 */
	acpigen_write_method("CFLR", 0);
	{
		acpigen_write_if_lequal_namestr_int("^CNVW.WFLR", 1);
		{
			acpigen_write_store_int_to_namestr(1, "^CNVW.WIFR");
		}
		acpigen_pop_len();
	}
	acpigen_pop_len();

/*
 *	Method (CNIP, 0, NotSerialized)
 *	{
 *		If (^CNVW.VDID != 0xFFFFFFFF)
 *		{
 *			Return (One)
 *		} Else {
 *			Return (Zero)
 *		}
 *	}
 */
	acpigen_write_method("CNIP", 0);
	{
		acpigen_write_if_lequal_namestr_int("^CNVW.VDID", 0xffffff);
		{
			acpigen_write_return_integer(0);
		}
		acpigen_write_else();
		{
			acpigen_write_return_integer(1);
		}
		acpigen_pop_len();
	}
	acpigen_pop_len();
}

static struct device_operations cnvi_wifi_ops = {
	.read_resources		= pci_dev_read_resources,
	.set_resources		= pci_dev_set_resources,
	.enable_resources	= pci_dev_enable_resources,
	.ops_pci		= &pci_dev_ops_pci,
	.scan_bus		= scan_static_bus,
	.acpi_name		= cnvi_wifi_acpi_name,
	.acpi_fill_ssdt		= cnvw_fill_ssdt,
};

static const unsigned short wifi_pci_device_ids[] = {
	PCI_DID_INTEL_PTL_H_CNVI_WIFI_0,
	PCI_DID_INTEL_PTL_H_CNVI_WIFI_1,
	PCI_DID_INTEL_PTL_H_CNVI_WIFI_2,
	PCI_DID_INTEL_PTL_H_CNVI_WIFI_3,
	PCI_DID_INTEL_PTL_U_H_CNVI_WIFI_0,
	PCI_DID_INTEL_PTL_U_H_CNVI_WIFI_1,
	PCI_DID_INTEL_PTL_U_H_CNVI_WIFI_2,
	PCI_DID_INTEL_PTL_U_H_CNVI_WIFI_3,
	PCI_DID_INTEL_LNL_CNVI_WIFI_0,
	PCI_DID_INTEL_LNL_CNVI_WIFI_1,
	PCI_DID_INTEL_LNL_CNVI_WIFI_2,
	PCI_DID_INTEL_LNL_CNVI_WIFI_3,
	PCI_DID_INTEL_MTL_CNVI_WIFI_0,
	PCI_DID_INTEL_MTL_CNVI_WIFI_1,
	PCI_DID_INTEL_MTL_CNVI_WIFI_2,
	PCI_DID_INTEL_MTL_CNVI_WIFI_3,
	PCI_DID_INTEL_CML_LP_CNVI_WIFI,
	PCI_DID_INTEL_CML_H_CNVI_WIFI,
	PCI_DID_INTEL_CNL_LP_CNVI_WIFI,
	PCI_DID_INTEL_CNL_H_CNVI_WIFI,
	PCI_DID_INTEL_GLK_CNVI_WIFI,
	PCI_DID_INTEL_JSL_CNVI_WIFI_0,
	PCI_DID_INTEL_JSL_CNVI_WIFI_1,
	PCI_DID_INTEL_JSL_CNVI_WIFI_2,
	PCI_DID_INTEL_JSL_CNVI_WIFI_3,
	PCI_DID_INTEL_TGL_CNVI_WIFI_0,
	PCI_DID_INTEL_TGL_CNVI_WIFI_1,
	PCI_DID_INTEL_TGL_CNVI_WIFI_2,
	PCI_DID_INTEL_TGL_CNVI_WIFI_3,
	PCI_DID_INTEL_TGL_H_CNVI_WIFI_0,
	PCI_DID_INTEL_TGL_H_CNVI_WIFI_1,
	PCI_DID_INTEL_TGL_H_CNVI_WIFI_2,
	PCI_DID_INTEL_TGL_H_CNVI_WIFI_3,
	PCI_DID_INTEL_ADL_P_CNVI_WIFI_0,
	PCI_DID_INTEL_ADL_P_CNVI_WIFI_1,
	PCI_DID_INTEL_ADL_P_CNVI_WIFI_2,
	PCI_DID_INTEL_ADL_P_CNVI_WIFI_3,
	PCI_DID_INTEL_ADL_S_CNVI_WIFI_0,
	PCI_DID_INTEL_ADL_S_CNVI_WIFI_1,
	PCI_DID_INTEL_ADL_S_CNVI_WIFI_2,
	PCI_DID_INTEL_ADL_S_CNVI_WIFI_3,
	PCI_DID_INTEL_ADL_N_CNVI_WIFI_0,
	PCI_DID_INTEL_ADL_N_CNVI_WIFI_1,
	PCI_DID_INTEL_ADL_N_CNVI_WIFI_2,
	PCI_DID_INTEL_ADL_N_CNVI_WIFI_3,
	PCI_DID_INTEL_RPL_S_CNVI_WIFI_0,
	PCI_DID_INTEL_RPL_S_CNVI_WIFI_1,
	PCI_DID_INTEL_RPL_S_CNVI_WIFI_2,
	PCI_DID_INTEL_RPL_S_CNVI_WIFI_3,
	0
};

static const struct pci_driver pch_cnvi_wifi __pci_driver = {
	.ops			= &cnvi_wifi_ops,
	.vendor			= PCI_VID_INTEL,
	.devices		= wifi_pci_device_ids,
};

static const char *cnvi_bt_acpi_name(const struct device *dev)
{
	return "CNVB";
}

static struct device_operations cnvi_bt_ops = {
	.read_resources		= pci_dev_read_resources,
	.set_resources		= pci_dev_set_resources,
	.enable_resources	= pci_dev_enable_resources,
	.ops_pci		= &pci_dev_ops_pci,
	.scan_bus		= scan_static_bus,
	.acpi_name		= cnvi_bt_acpi_name,
	.acpi_fill_ssdt		= acpi_device_write_pci_dev,
};

static const unsigned short bt_pci_device_ids[] = {
	PCI_DID_INTEL_TGL_CNVI_BT_0,
	PCI_DID_INTEL_TGL_CNVI_BT_1,
	PCI_DID_INTEL_TGL_CNVI_BT_2,
	PCI_DID_INTEL_TGL_CNVI_BT_3,
	PCI_DID_INTEL_TGL_H_CNVI_BT_0,
	PCI_DID_INTEL_TGL_H_CNVI_BT_1,
	PCI_DID_INTEL_TGL_H_CNVI_BT_2,
	0
};

static const struct pci_driver pch_cnvi_bt __pci_driver = {
	.ops			= &cnvi_bt_ops,
	.vendor			= PCI_VID_INTEL,
	.devices		= bt_pci_device_ids,
};
