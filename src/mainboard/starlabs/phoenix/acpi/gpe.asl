/* SPDX-License-Identifier: GPL-2.0-only */

External (\_SB.PCI0.GP41.XHC0, DeviceObj)
External (\_SB.PCI0.GP41.XHC1, DeviceObj)
External (\_SB.PCI0.GP43, DeviceObj)
External (\_SB.PCI0.GP43.XHC2, DeviceObj)
External (\_SB.PCI0.GP43.XHC3, DeviceObj)
External (\_SB.PCI0.GP43.NHI0, DeviceObj)
External (\_SB.PCI0.GP43.NHI1, DeviceObj)

Scope (\_GPE)
{
	Method (_L1F, 0, NotSerialized)
	{
		Notify (\_SB.PCI0.GP41.XHC0, 0x02)
		Notify (\_SB.PCI0.GP41.XHC1, 0x02)
		Notify (\_SB.PCI0.GP43, 0x02)
		Notify (\_SB.PCI0.GP43.XHC2, 0x02)
		Notify (\_SB.PCI0.GP43.XHC3, 0x02)
		Notify (\_SB.PCI0.GP43.NHI0, 0x02)
		Notify (\_SB.PCI0.GP43.NHI1, 0x02)
	}
}
