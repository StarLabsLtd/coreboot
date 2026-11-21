/* SPDX-License-Identifier: GPL-2.0-only */
#include <power_budget.h>

External (\_SB.PCI0.LPCB.EC, DeviceObj)
External (\_SB.PCI0.LPCB.EC.ECRD, MethodObj)
External (\_SB.PCI0.LPCB.EC.PBVR, FieldUnitObj)
External (\_SB.PCI0.LPCB.EC.PBWT, FieldUnitObj)

Scope (\_SB)
{
	OperationRegion (PXIO, SystemIO, 0xB2, 2)
	Field (PXIO, ByteAcc, NoLock, Preserve)
	{
		PXCM, 8,
		PXST, 8
	}

	/* One serialized operation for USB4 and the EC power-budget handoff. */
	Method (PXSM, 2, Serialized)
	{
		PXST = Arg1
		PXCM = Arg0
		Return (PXST)
	}

	Method (PBUD, 0, Serialized)
	{
		/* Old EC firmware does not implement this versioned interface. */
		If (\_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.PBVR)) != One) { Return () }
		Local0 = \_SB.PCI0.LPCB.EC.ECRD (RefOf (\_SB.PCI0.LPCB.EC.PBWT))
		If (Local0 == 0xFF) { Return () }
		If (\_SB.PXSM (PHX_POWER_BUDGET_APMC, Local0) != PHX_POWER_BUDGET_READY)
		{
			Debug = "Phoenix power budget handoff failed"
		}
	}
}
