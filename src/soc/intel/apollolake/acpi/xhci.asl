/* SPDX-License-Identifier: GPL-2.0-only */

/* XHCI Controller 0:15.0 */
Device (XHCI) {
	Name (_ADR, 0x00150000)  /* Device 21, Function 0 */

	Name (_S3D, 3)  /* D3 supported in S3 */
	Name (_S0W, 3)  /* D3 can wake device in S0 */
	Name (_S3W, 3)  /* D3 can wake system from S3 */

	/* Declare XHCI GPE status and enable bits are bit 13 */
	Name (_PRW, Package() { GPE0A_XHCI_PME_STS, 3 })

	OperationRegion (USBR, PCI_Config, 0x74, 0x02)
	Field (USBR, ByteAcc, NoLock, Preserve)
	{
		PMST, 2,	/* Power State */
		    , 1,	/* Reserved */
		NSRT, 1,	/* No Soft Reset */
		    , 4,	/* Reserved */
		PMEE, 1,	/* PME Enable */
		    , 6,	/* Reserved */
		PMES, 1,	/* PME Status */
	}

	Method (_PS0, 0, Serialized)
	{
		If ((PMST == 0x03))
		{
			Local0 = TSTM (0x00a28008, 0x00, 0x00)
			If (((Local0 & 0x00080000) != 0x00))
			{
				Local0 &= 0xfff7ffff
				TSTM (0x00a28008, Local0, 0x01)
			}
		}
	}

	Method (_PS3, 0, Serialized)
	{

	}

	Method (_STA, 0)
	{
		Return (0xF)
	}

	Device (RHUB)
	{
		/* Root Hub */
		Name (_ADR, 0)

#if CONFIG(SOC_INTEL_GEMINILAKE)
#include "xhci_glk_ports.asl"
#else
#include "xhci_apl_ports.asl"
#endif
	}
}
