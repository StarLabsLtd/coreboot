/* SPDX-License-Identifier: GPL-2.0-or-later */

OperationRegion (DPME, SystemMemory, BASE(_ADR), 0x100)
Field (DPME, AnyAcc, NoLock, Preserve)
{
	VDID, 32,
	Offset(0x84),   /* 0x84, DMA CFG PM CAP */
	PMST, 2,        /* 1:0, PM_STATE */
	, 6,
	PMEE, 1,        /* 8, PME_EN */
	, 6,
	PMES, 1,        /* 15, PME_STATUS */
	Offset(0xc8),   /* 0xC8, TBT NVM FW Revision */
	,     31,
	INFR,  1,       /* TBT NVM FW Ready */
	Offset(0xec),   /* 0xEC, TBT TO PCIE Register */
	TB2P, 32,       /* TBT to PCIe */
	P2TB, 32,       /* PCIe to TBT */
	Offset(0xfc),   /* 0xFC, DMA RTD3 Force Power */
	DD3E, 1,        /* 0:0 DMA RTD3 Enable */
	DFPE, 1,        /* 1:1 DMA Force Power */
	Offset (0xff),
	DMAD, 8         /* 31:24 DMA Active Delay */
}

/* IUHR: IOM USB4 Host Router */
OperationRegion (IUHR, SystemMemory, IOM_BASE_ADDR, 0x600)
Field (IUHR, DWordAcc, NoLock, Preserve)
{
	Offset(0x594),    /* USB4HR_MISC_CONFIG */
	U0D3, 1           /* [0:0] USB4HR0_RTD3_PWR_EN */
}

OperationRegion (DPEE, SystemMemory, BASE(_ADR), 0x100)
Field (DPEE, AnyAcc, NoLock, Preserve)
{
	Offset (0x00),
	Offset (0x84),
	XPME,   16
}

Name (STAT, 0x1)  /* Variable to save power state 1 - D0, 0 - D3C */
Name (PMCS, Zero)

Method (_S0W, 0x0)
{
#if CONFIG(D3COLD_SUPPORT)
	Printf("TDM0  _S0W: 0x04")
	Return (0x04)
#else
	Printf("TDM0  _S0W: 0x03")
	Return (0x03)
#endif	// D3COLD_SUPPORT
}

/*
 * Get power resources that are dependent on this device for Operating System Power Management
 * to put the device in the D0 device state
 */
Method (_PR0)
{
#if CONFIG(D3COLD_SUPPORT)
	Printf("TDM0  _PR0: D3C")
	Return (Package() { \_SB.PCI0.D3C, \_SB.PCI0.TBT0 })
#else
	Printf("TDM0  _PR0: D3Hot")
	Return (Package() { \_SB.PCI0.TBT0 })
#endif	// D3COLD_SUPPORT
}

Method (_PR3)
{
#if CONFIG(D3COLD_SUPPORT)
	Printf("TDM0  _PR3: D3C")
	Return (Package() { \_SB.PCI0.D3C, \_SB.PCI0.TBT0 })

#else
	Printf("TDM0  _PR3: D3Hot")
	Return (Package() { \_SB.PCI0.TBT0 })
#endif	// D3COLD_SUPPORT
}

/*
 * RTD3 Exit Method to bring TBT controller out of RTD3 mode.
 */
Method (D3CX, 0, Serialized)
{

	U0D3 = 0x01	    /* Disable RTD3, power on USB4 HR */
	STAT = 0x01

	If (CondRefOf (VDID))
	{
		Local0 = 0xC8
		Local1 = Zero
		While (((STAT == One) && (VDID == 0xFFFFFFFF)))
		{
			If ((Local1 == 0x001E8480))
			{
				Printf("TDM0 D3CX Timeout. ")
				Break
			}
			Else
			{
				Stall (Local0)
				Local1 += 0xC8
			}
		}
	}

	Local2 = XPME
	If ((PMCS == Zero))
	{
		Printf("TDM0 D3CX PMCS is 0x0, not restoring PMCSR")
	}
	Else
	{
		If (((Local2 & 0x03) != (PMCS & 0x03)))
		{
			Local3 = (PMCS & 0x7FFF)
			XPME = Local3
			PMCS = Zero
		}
	}
}

/*
 * RTD3 Entry method to enable TBT controller RTD3 mode.
 */
Method (D3CE, 0, Serialized)
{
	U0D3 = 0x0	     /* Enable RTD3, power off USB4 HR */
	STAT = 0x00
	PMCS = XPME
}

/*
 * Variable to skip TCSS/TBT D3 cold; 1+: Skip D3CE, 0 - Enable D3CE
 * TCSS D3 Cold and TBT RTD3 is only available when system power state is in S0.
 */
Name (SD3C, 0)

Method (_DSW, 3)
{
	/* If entering Sx (Arg1 > 1), need to skip TCSS D3Cold & TBT RTD3/D3Cold. */
	SD3C = Arg1
}

Method (_PRW, 0)
{
	Return (Package() { 0x65, TCSS_WAKE_LOWEST_SLEEP_STATE })
}
