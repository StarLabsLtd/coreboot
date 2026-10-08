/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Native PCI power management owns these devices' D-state transitions. */
Scope (GP0A)
{
	STAT = 0x0F
	Device (NVME)
	{
		Name (_ADR, Zero)
	}
}

Scope (GP14)
{
	Device (NVME)
	{
		Name (_ADR, Zero)
	}
}

Scope (GP41)
{
	Device (HDAU) { Name (_ADR, One) }
	Device (AZAL) { Name (_ADR, 0x06) }
	Device (MP2C) { Name (_ADR, 0x07) }
}

Scope (GP12)
{
	Name (_S0W, 0x03)
	Method (_PRW, 0, NotSerialized)
	{
		Return (Package () { 0x0E, 0x04 })
	}
}
