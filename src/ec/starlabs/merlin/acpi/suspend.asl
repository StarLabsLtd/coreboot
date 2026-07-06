/* SPDX-License-Identifier: GPL-2.0-only */

#if CONFIG(STARLABS_ACPI_EFI_OPTION_SMI)
Field (\DNVS, ByteAcc, NoLock, Preserve)
{
	EOCM, 32,	// EFI option command
	EOID, 32,	// EFI option ID
	EOVL, 32,	// EFI option value
	EORS, 32,	// EFI option status
}

Name (EOAP, 0xE2)	// STARLABS_APMC_CMD_EFI_OPTION

Name (EOFL, 0x1)	// STARLABS_EFIOPT_ID_FN_LOCK_STATE
Name (EOTP, 0x2)	// STARLABS_EFIOPT_ID_TRACKPAD_STATE
Name (EOKB, 0x3)	// STARLABS_EFIOPT_ID_KBL_BRIGHTNESS
Name (EOKS, 0x4)	// STARLABS_EFIOPT_ID_KBL_STATE
Name (EOKT, 0x5)	// STARLABS_EFIOPT_ID_KBL_TIMEOUT

Mutex (EOMX, 0x00)

Method (EOGT, 1, Serialized)
{
	If (Acquire (EOMX, 1000))
	{
		Return (0xFFFFFFFF)
	}

	Store (0x01, EOCM)
	Store (Arg0, EOID)
	Store (0xFFFFFFFF, EOVL)
	Store (0xFFFFFFFF, EORS)
	Store (EOAP, \_SB.PCI0.LPCB.EC.SMB2)

	If (EORS)
	{
		Store (0xFFFFFFFF, Local0)
	}
	Else
	{
		Store (EOVL, Local0)
	}
	Release (EOMX)
	Return (Local0)
}

Method (EOMS, 0, Serialized)
{
	If (Acquire (EOMX, 1000))
	{
		Return (0x00)
	}

	Store (0x03, EOCM)
	Store (0x00, EOVL)
	Store (0xFFFFFFFF, EORS)
	Store (EOAP, \_SB.PCI0.LPCB.EC.SMB2)

	If (EORS)
	{
		Store (0x00, Local0)
	}
	Else
	{
		Store (EOVL, Local0)
	}
	Release (EOMX)
	Return (Local0)
}

Method (EOSV, 2, Serialized)
{
	If (Acquire (EOMX, 1000))
	{
		Return (0x01)
	}

	Store (0x02, EOCM)
	Store (Arg0, EOID)
	Store (Arg1, EOVL)
	Store (0xFFFFFFFF, EORS)
	Store (EOAP, \_SB.PCI0.LPCB.EC.SMB2)

	Store (EORS, Local0)
	Release (EOMX)
	Return (Local0)
}
#endif

Method (RPTS, 1, Serialized)
{

	/*
	 * Disable ACPI support.
	 * This should always be the last action before entering a sleep state.
	 */
	\_SB.PCI0.LPCB.EC.ECWR(0x00, RefOf(\_SB.PCI0.LPCB.EC.OSFG))

	Return (Arg0)
}

Method (RWAK, 1, Serialized)
{
	/*
	 * Enable ACPI support.
	 * This should always be the first action when exiting a sleep state.
	 */
	\_SB.PCI0.LPCB.EC.ECWR(0x01, RefOf(\_SB.PCI0.LPCB.EC.OSFG))

#if CONFIG(STARLABS_ACPI_EFI_OPTION_SMI)
	/* Restore EC settings from UEFI variable store */
	Store (EOGT (EOTP), Local0)
	If (Local0 == 0x22)
	{
		\_SB.PCI0.LPCB.EC.ECWR (0x22, RefOf(\_SB.PCI0.LPCB.EC.TPLE))
	}
	Else
	{
		\_SB.PCI0.LPCB.EC.ECWR (0x00, RefOf(\_SB.PCI0.LPCB.EC.TPLE))
	}

	\_SB.PCI0.LPCB.EC.ECWR (EOGT (EOFL), RefOf(\_SB.PCI0.LPCB.EC.FLKE))

	Store (EOGT (EOKS), Local0)
	If (Local0 == 0xdd)
	{
		\_SB.PCI0.LPCB.EC.ECWR (0xdd, RefOf(\_SB.PCI0.LPCB.EC.KLSE))
	}
	Else
	{
		\_SB.PCI0.LPCB.EC.ECWR (0x00, RefOf(\_SB.PCI0.LPCB.EC.KLSE))
	}

	Store (EOGT (EOKB), Local0)
	Switch (ToInteger (Local0))
	{
		Case (0xdd)
		{
			\_SB.PCI0.LPCB.EC.ECWR (0xdd, RefOf(\_SB.PCI0.LPCB.EC.KLBE))
		}
		Case (0xcc)
		{
			\_SB.PCI0.LPCB.EC.ECWR (0xcc, RefOf(\_SB.PCI0.LPCB.EC.KLBE))
		}
		Case (0xbb)
		{
			\_SB.PCI0.LPCB.EC.ECWR (0xbb, RefOf(\_SB.PCI0.LPCB.EC.KLBE))
		}
		Case (0xaa)
		{
			\_SB.PCI0.LPCB.EC.ECWR (0xaa, RefOf(\_SB.PCI0.LPCB.EC.KLBE))
		}
	}

	Store (EOGT (EOKT), Local0)
	If (Local0 <= 0x04)
	{
		\_SB.PCI0.LPCB.EC.ECWR (Local0, RefOf(\_SB.PCI0.LPCB.EC.KLTE))
	}
#endif

	Return (Arg0)
}
