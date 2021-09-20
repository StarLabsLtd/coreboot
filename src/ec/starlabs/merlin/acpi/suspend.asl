/* SPDX-License-Identifier: GPL-2.0-only */

Method (RPTS, 1, NotSerialized)
{
	\_SB.PCI0.LPCB.H_EC.OSFG = 0x00
	WDMS (\_SB.PCI0.LPCB.H_EC.KLBS, \_SB.PCI0.LPCB.KLBS)
	If ((Arg0 == 0x04) || (Arg0 == 0x05))
	{
		FLKS = FLKA
		TPLS = TPLA
		KLBC = KLBE
		KLSC = KLSE

	}

	If ((Arg0 == 0x03))
	{
		If (CondRefOf (\_SB.DTSE))
		{
			If ((\_SB.DTSE && (TCNT > One)))
			{
				TRAP (0x02, 0x1E)
			}
		}

		CWEF = CPWE
	}

	If (CondRefOf (\_SB.TPM.PTS))
	{
		\_SB.TPM.PTS (Arg0)
	}

	If (CondRefOf (\_SB.PCI0.TXHC))
	{
		If (TRTD)
		{
			\_SB.PCI0.TCON ()
		}

		If (ITRT)
		{
			\_SB.PCI0.TG0N ()
			\_SB.PCI0.TG1N ()
		}
	}
}


Method (RWAK, 1, Serialized)
{
	\_SB.PCI0.LPCB.H_EC.OSFG = One
	If (NEXP)
	{
		If ((OSCC & One))
		{
			NHPG ()
		}

		If ((OSCC & 0x04))
		{
			NPME ()
		}
	}

	If ((Arg0 == 0x03))
	{
		If ((Zero == ACTT))
		{
			If ((ECON == One))
			{
				\_SB.PCI0.LPCB.H_EC.ECWT (Zero, RefOf (\_SB.PCI0.LPCB.H_EC.CFAN))
			}
		}
	}

	If (((Arg0 == 0x03) || (Arg0 == 0x04)))
	{
		If ((GBSX & 0x40))
		{
			\_SB.PCI0.GFX0.IUEH (0x06)
		}

		If ((GBSX & 0x80))
		{
			\_SB.PCI0.GFX0.IUEH (0x07)
		}

		If (CondRefOf (\_SB.DTSE))
		{
			If ((\_SB.DTSE && (TCNT > One)))
			{
				TRAP (0x02, 0x14)
			}
		}

		If ((\_SB.PCI0.RP01.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP01, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP02.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP02, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP03.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP03, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP04.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP04, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP05.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP05, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP06.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP06, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP07.VDID != 0xFFFFFFFF))
		{
			If ((DSTS == Zero))
			{
				Notify (\_SB.PCI0.RP07, Zero) // Bus Check
			}
		}

		If ((\_SB.PCI0.RP08.VDID != 0xFFFFFFFF))
		{
			If ((DSTS == Zero))
			{
				Notify (\_SB.PCI0.RP08, Zero) // Bus Check
			}
		}

		If ((\_SB.PCI0.RP09.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP09, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP10.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP10, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP11.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP11, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP12.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP12, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP13.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP13, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP14.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP14, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP15.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP15, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP16.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP16, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP17.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP17, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP18.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP18, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP19.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP19, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP20.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP20, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP21.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP21, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP22.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP22, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP23.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP23, Zero) // Bus Check
		}

		If ((\_SB.PCI0.RP24.VDID != 0xFFFFFFFF))
		{
			Notify (\_SB.PCI0.RP24, Zero) // Bus Check
		}

		If (CondRefOf (\_SB.PCI0.TXHC))
		{
			\_SB.TCWK (Arg0)
		}
        }

        If (Arg0 == 0x03)
        {
		If (CondRefOf (\_SB.NVDR.RSTP))
		{
			\_SB.NVDR.RSTP ()
		}
        }

        Return (Package (0x02)
        {
		Zero, 
		Zero
        })
}

