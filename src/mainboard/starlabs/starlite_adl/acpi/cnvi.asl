Scope (\_SB.PCI0)
{
	Device (CNVW)
	{
		Name (_ADR, 0x00140003)  // _ADR: Address
		Name (RSTT, Zero)
		Name (PRRS, Zero)
		OperationRegion (CWAR, SystemMemory, (GPCB() + 0xa3000), 0x100)
		Field (CWAR, WordAcc, NoLock, Preserve)
		{
			VDID,   32, 
			    ,   1, 
			WMSE,   1, 
			WBME,   1, 
			Offset (0x10), 
			WBR0,   64, 
			Offset (0x44), 
			    ,   28, 
			WFLR,   1, 
			Offset (0x48), 
			    ,   15, 
			WIFR,   1, 
			Offset (0xCC), 
			WPMS,   32
		}

		Field (CWAR, ByteAcc, NoLock, Preserve)
		{
			Offset (0xCC), 
			Offset (0xCD), 
			PMEE,   1, 
			    ,   6, 
			PMES,   1
		}

		PowerResource (WRST, 0x05, 0x0000)
		{
			Method (_STA, 0, NotSerialized)  // _STA: Status
			{
				Return (One)
			}

			Method (_ON, 0, NotSerialized)  // _ON_: Power On
			{
			}

			Method (_OFF, 0, NotSerialized)  // _OFF: Power Off
			{
			}

			Method (_RST, 0, NotSerialized)  // _RST: Device Reset
			{
				If (^^WFLR == 1)
				{
					WBR0 = 0
					WPMS = 0
					WBME = 0
					WMSE = 0
					WIFR = 1
				}
			}
		}

		Method (_S0W, 0, NotSerialized)  // _S0W: S0 Device Wake State
		{
			Return (0x03)
		}

		// Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
		// {
		//	Return (GPRW (0x6D, 0x04))
		// }

		Method (GPEH, 0, NotSerialized)
		{
//			If ((VDID == 0xFFFFFFFF))
//			{
//				Return (Zero)
//			}

			If ((PMES == One))
			{
				Notify (CNVW, 0x02) // Device Wake
			}
		}

		Method (_PS0, 0, Serialized)  // _PS0: Power State 0
		{
		}

		Method (_PS3, 0, Serialized)  // _PS3: Power State 3
		{
		}

		Method (_DSW, 3, NotSerialized)  // _DSW: Device Sleep Wake
		{
		}

		Name (_PRR, Package (0x01)  // _PRR: Power Resource for Reset
		{
			WRST
		})
	}

	Method (CFLR, 0, NotSerialized)
	{
		If (^CNVW.WFLR == One)
		{
			^CNVW.WIFR = One
		}
	}

	Method (CNIP, 0, NotSerialized)
	{
		// TODO: Check if cnvi enabled in devicetree
		If (^CNVW.VDID != 0xFFFFFFFF)
		{
			Return (One)
		} Else {
			Return (Zero)
		}
	}

	Method (SBTE, 1, Serialized)
	{
		Local0 = GBTP()
		Printf ("SBTE Arg: %o", Arg0)
		If (Arg0 == 1)
		{
			STXS(Local0)
		} Else {
			CTXS(Local0)
		}
	}

	Method (GBTE, 0, NotSerialized)
	{
		Local0 = GBTP()
		Return (GTXS (Local0))
	}

	Method (AOLX, 0, NotSerialized)
	{
		Name (AODS, Package (0x03)
		{
			Zero, 
			0x12, 
			Zero
		})
		// TODO: If Audio offload enabled
		// {
			AODS [0x02] = One
		// }
		Return (AODS) /* \_SB_.PC00.AOLX.AODS */
	}
}


Scope (_SB)
{
	Method (GBTP, 0, Serialized)
	{
#if CONFIG(SOC_INTEL_ALDERLAKE_PCH_S)
		Return (0x08030000)
#else
		Return (0x090A0000)
#endif
	}
}
