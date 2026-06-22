/* SPDX-License-Identifier: GPL-2.0-only */

#include <soc/iomap.h>

#define AWAC_GPE		0x72
#define AWAC_WADT_AC		0x1800
#define AWAC_GCP_S4		0xb7
#define AWAC_GCP_S5		0x1f7

#if CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_KEEP_WADT_ENABLED_IN_S5)
#define AWAC_LOWEST_SLEEP_STATE	5
#define AWAC_GCP		AWAC_GCP_S5
#else
#define AWAC_LOWEST_SLEEP_STATE	4
#define AWAC_GCP		AWAC_GCP_S4
#endif

Scope (\_SB.PCI0.LPCB.RTC)
{
	OperationRegion (RTCM, SystemCMOS, 0, 0x3f)
	Field (RTCM, ByteAcc, Lock, Preserve)
	{
		Offset (0), SEC, 8,
		Offset (2), MIN, 8,
		Offset (4), HOR, 8,
		Offset (7), DAY, 8,
		Offset (8), MON, 8,
		Offset (9), YEAR, 8,
		Offset (0x0a), REGA, 8,
		Offset (0x32), CNTY, 8,
	}

	Mutex (RTCL, 0)

	Method (GRTT, 0, Serialized)
	{
		Name (BUFF, Buffer (0x10) {})
		CreateWordField (BUFF, 0x0, Y)
		CreateByteField (BUFF, 0x2, M)
		CreateByteField (BUFF, 0x3, D)
		CreateByteField (BUFF, 0x4, H)
		CreateByteField (BUFF, 0x5, MNT)
		CreateByteField (BUFF, 0x6, S)
		CreateByteField (BUFF, 0x7, V)
		CreateWordField (BUFF, 0xa, TZ)
		CreateByteField (BUFF, 0xc, DL)

		Acquire (RTCL, 0xffff)
		Local0 = 0
		While ((REGA & 0x80) && (Local0 < 100))
		{
			Stall (10)
			Local0 += 10
		}

		Y = (FromBCD (CNTY) * 100) + FromBCD (YEAR)
		M = FromBCD (MON)
		D = FromBCD (DAY)
		H = FromBCD (HOR)
		MNT = FromBCD (MIN)
		S = FromBCD (SEC)
		Release (RTCL)

		TZ = 2047
		DL = 0
		V = 1
		Return (BUFF)
	}

	Method (SRTT, 1, Serialized)
	{
		CreateWordField (Arg0, 0x0, Y)
		CreateByteField (Arg0, 0x2, M)
		CreateByteField (Arg0, 0x3, D)
		CreateByteField (Arg0, 0x4, H)
		CreateByteField (Arg0, 0x5, MNT)
		CreateByteField (Arg0, 0x6, S)

		Acquire (RTCL, 0xffff)
		Local0 = 0
		While ((REGA & 0x80) && (Local0 < 100))
		{
			Stall (10)
			Local0 += 10
		}

		If (Local0 >= 100)
		{
			Release (RTCL)
			Return (0xffffffff)
		}

		Local1 = Y % 100
		Local2 = Y / 100
		YEAR = ToBCD (Local1)
		CNTY = ToBCD (Local2)
		MON = ToBCD (M)
		DAY = ToBCD (D)
		HOR = ToBCD (H)
		MIN = ToBCD (MNT)
		SEC = ToBCD (S)
		Release (RTCL)

		Return (0)
	}
}

Scope (\_SB)
{
	OperationRegion (WARM, SystemMemory, PCH_PWRM_BASE_ADDRESS, PCH_PWRM_BASE_SIZE)
	Field (WARM, DWordAcc, NoLock, Preserve)
	{
		Offset (AWAC_WADT_AC),
		ACWA, 32,
		DCWA, 32,
		ACET, 32,
		DCET, 32,
	}

	Device (AWAC)
	{
		Name (_HID, "ACPI000E")
		Name (WAST, 0)
		Name (WTTR, 0)

		Method (_PRW, 0)
		{
			Return (Package () { AWAC_GPE, AWAC_LOWEST_SLEEP_STATE })
		}

		Method (_STA, 0)
		{
			Return (0xf)
		}

		Method (_GCP, 0)
		{
			Return (AWAC_GCP)
		}

		Method (_GRT, 0, Serialized)
		{
			Return (\_SB.PCI0.LPCB.RTC.GRTT ())
		}

		Method (_SRT, 1, Serialized)
		{
			Return (\_SB.PCI0.LPCB.RTC.SRTT (Arg0))
		}

		Method (_GWS, 1, Serialized)
		{
			Local0 = 0

			If (Arg0 == 0)
			{
				If ((ACWA == 0xffffffff) && (WTTR & 1))
				{
					Local0 |= 1
					WTTR ^= 1
				}
			}
			ElseIf ((DCWA == 0xffffffff) && (WTTR & 2))
			{
				Local0 |= 1
				WTTR ^= 2
			}

			If (WAST)
			{
				Local0 |= 2
				WAST = 0
			}

			Return (Local0)
		}

		Method (_CWS, 1)
		{
			Return (0)
		}

		Method (_STP, 2)
		{
			If (Arg0 == 0)
			{
				ACET = Arg1
			}
			Else
			{
				DCET = Arg1
			}

			Return (0)
		}

		Method (_STV, 2, Serialized)
		{
			If (Arg0 == 0)
			{
				ACWA = Arg1
				WTTR |= 1
			}
			Else
			{
				DCWA = Arg1
				WTTR |= 2
			}

			Return (0)
		}

		Method (_TIP, 1)
		{
			If (Arg0 == 0)
			{
				Return (ACET)
			}

			Return (DCET)
		}

		Method (_TIV, 1)
		{
			If (Arg0 == 0)
			{
				Return (ACWA)
			}

			Return (DCWA)
		}
	}
}

Scope (\_GPE)
{
	Method (_L72, 0, Serialized)
	{
		If (CondRefOf (\_SB.AWAC))
		{
			\_SB.AWAC.WAST = 1
			Notify (\_SB.AWAC, 0x02)
		}
	}
}
