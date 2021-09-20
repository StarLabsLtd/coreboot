/* SPDX-License-Identifier: GPL-2.0-only */

Method (_PTS, 1, NotSerialized)				// _PTS: Prepare To Sleep
{
	If (Arg0)
	{
		\_SB.TPM.TPTS (Arg0)
		RPTS (Arg0)
		\_SB.PC00.LPCB.SPTS (Arg0)
	}
}

Method (_WAK, 1, NotSerialized)				// _WAK: Wake
{
	\_SB.PC00.LPCB.SWAK (Arg0)
	RWAK (Arg0)
	Return (AM00) /* \AM00 */
}
