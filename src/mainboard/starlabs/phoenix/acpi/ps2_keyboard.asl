/* SPDX-License-Identifier: GPL-2.0-or-later */

Device (PS2K)
{
	Name (_HID, EISAID (CONFIG_PS2K_EISAID))
	Name (_CID, EISAID ("PNP030B"))
	Name (_UID, 0)

	Method (_STA, 0, NotSerialized)
	{
		Return (0x0F)
	}

	Name (_CRS, ResourceTemplate ()
	{
		IO (Decode16,
			0x0060, 0x0060, 0x00, 0x01)
		IO (Decode16,
			0x0064, 0x0064, 0x00, 0x01)
		IRQ (Edge, ActiveLow, Shared)
		{
			1
		}
	})

	Method (_PSW, 1, NotSerialized)
	{
		KBFG = Arg0
	}
}
