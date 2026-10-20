/* SPDX-License-Identifier: GPL-2.0-only */

#define CONFIG(option) 1
DefinitionBlock ("", "SSDT", 2, "COREv4", "SMMGPIO", 1)
{
	Scope (\_SB)
	{
		Device (PCI0)
		{
			Name (_ADR, 0)
			Method (GADD, 1, NotSerialized)
			{
				Return (0x2000 + (Arg0 * 8))
			}
			OperationRegion (GNVS, SystemMemory, 0x1000, 0x43)
			Field (GNVS, ByteAcc, NoLock, Preserve)
			{
				Offset (0x3c),
				GPAD, 32,
				GPOP, 8,
				GPVL, 8,
				GPST, 8,
			}

			Name (TSTS, 0)
			Name (TSVL, 0)
			Name (TCNT, 0)
			Name (TNRP, 0)
			Method (GSMI, 0, Serialized)
			{
				Increment (TCNT)
				If (TNRP)
				{
					Return ()
				}
				GPVL = TSVL
				GPST = TSTS
			}

#include "../../src/soc/intel/common/block/acpi/acpi/gpio_op.asl"
		}
	}
}
