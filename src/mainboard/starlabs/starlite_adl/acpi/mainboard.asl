/* SPDX-License-Identifier: GPL-2.0-only */

Scope (\_SB) {
	#include "sleep.asl"
	#include "cnvi.asl"
}

Scope (_GPE)
{
	Method (_L0F, 0, NotSerialized)
	{
		Printf ("L0F")
		\_SB.PCI0.LPCB.EC.VBTN.UPDK()
	}
}
