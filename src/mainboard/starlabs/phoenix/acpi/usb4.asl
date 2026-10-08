/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * The USB4 NHI devices live below GP43, while the tunneled PCIe root ports
 * are GP19 and GP21. Linux needs ACPI links from the tunneled native ports
 * back to the NHI so the NHI resumes before the tunneled devices.
 */

#define USB4_WAKE_GPE	0x1F

/*
 * Native PCI power management remains available. Do not advertise an ACPI
 * D3cold power resource without a platform-controlled power-off mechanism.
 * A software-only _ON/_OFF pair cannot provide that handoff.
 */
#define USB4_PCI_POWER(_s0w, _wake_state) \
	Name (_S0W, _s0w) \
	Method (_PRW, 0, NotSerialized) \
	{ \
		Return (Package () { USB4_WAKE_GPE, _wake_state }) \
	}

Scope (\_SB.PCI0.GP43)
{
	OperationRegion (U4SM, SystemIO, 0xB2, 2)
	Field (U4SM, ByteAcc, NoLock, Preserve)
	{
		U4CM, 8,
		U4ST, 8
	}

	/* Serialized across both controllers. SMM accepts only these two commands. */
	Method (U4PS, 1, Serialized)
	{
		U4ST = 0xFF
		If (Arg0 == Zero)
		{
			U4CM = 0xE5
		}
		Else
		{
			U4CM = 0xE6
		}
		If (U4ST != Zero)
		{
			Debug = "USB4 PS3 handoff failed"
		}
	}

	Device (NHI0)
	{
		Name (_ADR, 0x05)
		Method (_STA, 0, NotSerialized)
		{
			Return (0x0F)
		}

		USB4_PCI_POWER (0x03, 0x03)
		Method (_PS0, 0, NotSerialized) {}
		Method (_PS3, 0, Serialized) { \_SB.PCI0.GP43.U4PS (Zero) }
	}

	Device (NHI1)
	{
		Name (_ADR, 0x06)
		Method (_STA, 0, NotSerialized)
		{
			Return (0x0F)
		}

		USB4_PCI_POWER (0x03, 0x03)
		Method (_PS0, 0, NotSerialized) {}
		Method (_PS3, 0, Serialized) { \_SB.PCI0.GP43.U4PS (One) }
	}
}

Scope (\_SB.PCI0.GP19)
{
	USB4_PCI_POWER (0x03, 0x04)

	Method (_DEP, 0, Serialized)
	{
		Return (Package () { \_SB.PCI0.GP43.NHI0 })
	}

	Name (_DSD, Package ()
	{
		ToUUID ("6211e2c0-58a3-4af3-90e1-927a4e0c55a4"),
		Package ()
		{
			Package () { "HotPlugSupportInD3", 1 },
		},

		ToUUID ("efcc06cc-73ac-4bc3-bff0-76143807c389"),
		Package ()
		{
			Package () { "ExternalFacingPort", 1 },
		},

		ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
		Package ()
		{
			Package () { "usb4-host-interface", \_SB.PCI0.GP43.NHI0 },
			Package () { "usb4-port-number", 0 },
		},
	})
}

Scope (\_SB.PCI0.GP21)
{
	USB4_PCI_POWER (0x03, 0x04)

	Method (_DEP, 0, Serialized)
	{
		Return (Package () { \_SB.PCI0.GP43.NHI1 })
	}

	Name (_DSD, Package ()
	{
		ToUUID ("6211e2c0-58a3-4af3-90e1-927a4e0c55a4"),
		Package ()
		{
			Package () { "HotPlugSupportInD3", 1 },
		},

		ToUUID ("efcc06cc-73ac-4bc3-bff0-76143807c389"),
		Package ()
		{
			Package () { "ExternalFacingPort", 1 },
		},

		ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
		Package ()
		{
			Package () { "usb4-host-interface", \_SB.PCI0.GP43.NHI1 },
			Package () { "usb4-port-number", 0 },
		},
	})
}
