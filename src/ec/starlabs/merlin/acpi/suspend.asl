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
Name (EOFC, 0x6)	// STARLABS_EFIOPT_ID_FN_CTRL_SWAP
Name (EOMC, 0x7)	// STARLABS_EFIOPT_ID_MAX_CHARGE
Name (EOFM, 0x8)	// STARLABS_EFIOPT_ID_FAN_MODE
Name (EOCS, 0x9)	// STARLABS_EFIOPT_ID_CHARGING_SPEED
Name (EOLS, 0x0A)	// STARLABS_EFIOPT_ID_LID_SWITCH
Name (EOPL, 0x0B)	// STARLABS_EFIOPT_ID_POWER_LED
Name (EOCL, 0x0C)	// STARLABS_EFIOPT_ID_CHARGE_LED
Name (EOPA, 0x0D)	// STARLABS_EFIOPT_ID_POWER_ON_AC

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

#if CONFIG(STARLABS_ACPI_EFI_OPTION_SMI)
	/* Store current EC settings in UEFI variable store */
	Store (\_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.TPLE)), Local0)
	If (Local0 == 0x11)
	{
		Store (0x00, Local0)
	}
	EOSV (EOTP, Local0)

	EOSV (EOFL, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.FLKE)))
	EOSV (EOKS, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.KLSE)))
	EOSV (EOKB, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.KLBE)))
	EOSV (EOKT, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.KLTE)))
	EOSV (EOFC, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.FCLA)))

#if CONFIG(EC_STARLABS_MAX_CHARGE)
	EOSV (EOMC, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.BFCP)))
#endif
#if CONFIG(EC_STARLABS_FAN)
	EOSV (EOFM, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.FANM)))
#endif
#if CONFIG(EC_STARLABS_CHARGING_SPEED)
	EOSV (EOCS, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.CHSP)))
#endif
#if CONFIG(EC_STARLABS_LID_SWITCH)
	EOSV (EOLS, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.LIDC)))
#endif
#if CONFIG(EC_STARLABS_POWER_LED)
	EOSV (EOPL, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.PLED)))
#endif
#if CONFIG(EC_STARLABS_CHARGE_LED)
	EOSV (EOCL, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.CHLE)))
#endif
#if CONFIG(EC_STARLABS_ADAPTER_AUTO_POWER_ON)
	EOSV (EOPA, \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.POAC)))
#endif
#endif

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

	Store (EOGT (EOFC), Local0)
	If (Local0 <= 0x01)
	{
		\_SB.PCI0.LPCB.EC.ECWR (Local0, RefOf(\_SB.PCI0.LPCB.EC.FCLA))
	}

#if CONFIG(EC_STARLABS_MAX_CHARGE)
	Store (EOGT (EOMC), Local0)
	Switch (ToInteger (Local0))
	{
		Case (0x00)
		{
			\_SB.PCI0.LPCB.EC.ECWR (0x00, RefOf(\_SB.PCI0.LPCB.EC.BFCP))
		}
		Case (0xbb)
		{
			\_SB.PCI0.LPCB.EC.ECWR (0xbb, RefOf(\_SB.PCI0.LPCB.EC.BFCP))
		}
		Case (0xaa)
		{
			\_SB.PCI0.LPCB.EC.ECWR (0xaa, RefOf(\_SB.PCI0.LPCB.EC.BFCP))
		}
	}
#endif

#if CONFIG(EC_STARLABS_FAN)
	Store (EOGT (EOFM), Local0)
	Switch (ToInteger (Local0))
	{
		Case (0x00)
		{
			\_SB.PCI0.LPCB.EC.ECWR (0x00, RefOf(\_SB.PCI0.LPCB.EC.FANM))
		}
		Case (0xbb)
		{
			\_SB.PCI0.LPCB.EC.ECWR (0xbb, RefOf(\_SB.PCI0.LPCB.EC.FANM))
		}
		Case (0xaa)
		{
			\_SB.PCI0.LPCB.EC.ECWR (0xaa, RefOf(\_SB.PCI0.LPCB.EC.FANM))
		}
		Case (0xcc)
		{
			\_SB.PCI0.LPCB.EC.ECWR (0xcc, RefOf(\_SB.PCI0.LPCB.EC.FANM))
		}
	}
#endif

#if CONFIG(EC_STARLABS_CHARGING_SPEED)
	Store (EOGT (EOCS), Local0)
	If (Local0 <= 0x02)
	{
		\_SB.PCI0.LPCB.EC.ECWR (Local0, RefOf(\_SB.PCI0.LPCB.EC.CHSP))
	}
#endif

#if CONFIG(EC_STARLABS_LID_SWITCH)
	Store (EOGT (EOLS), Local0)
	If (Local0 <= 0x02)
	{
		\_SB.PCI0.LPCB.EC.ECWR (Local0, RefOf(\_SB.PCI0.LPCB.EC.LIDC))
	}
#endif

#if CONFIG(EC_STARLABS_POWER_LED)
	Store (EOGT (EOPL), Local0)
	If (Local0 <= 0x02)
	{
		\_SB.PCI0.LPCB.EC.ECWR (Local0, RefOf(\_SB.PCI0.LPCB.EC.PLED))
	}
#endif

#if CONFIG(EC_STARLABS_CHARGE_LED)
	Store (EOGT (EOCL), Local0)
	If (Local0 <= 0x02)
	{
		\_SB.PCI0.LPCB.EC.ECWR (Local0, RefOf(\_SB.PCI0.LPCB.EC.CHLE))
	}
#endif
#if CONFIG(EC_STARLABS_ADAPTER_AUTO_POWER_ON)
	Store (EOGT (EOPA), Local0)
	If (Local0 <= 0x01)
	{
		\_SB.PCI0.LPCB.EC.ECWR (Local0, RefOf(\_SB.PCI0.LPCB.EC.POAC))
	}
#endif
#endif

	Return (Arg0)
}
