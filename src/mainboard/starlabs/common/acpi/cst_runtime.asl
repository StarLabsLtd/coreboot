/* SPDX-License-Identifier: GPL-2.0-only */

#include <common/touchpad.h>
#include <starlabs/cfr_runtime.h>

#define CST_BYTE(buffer, index) DerefOf (Index (buffer, index))
#define CST_WORD(buffer, index) (CST_BYTE (buffer, index) | (CST_BYTE (buffer, (index) + 1) << 8))
#define CST_HID_LENGTH (STARLABS_TOUCHPAD_CST_REPORT_LEN + 3)
#define CST_WRITE_LENGTH (STARLABS_TOUCHPAD_CST_REPORT_LEN + 9)
#define CST_WRITE_BUFFER (CST_WRITE_LENGTH + 2)
#define CST_PAYLOAD_INDEX 5
#define CST_COMMAND_INDEX 11

Scope (\_SB.PCI0.I2C0)
{
	Name (TCON, ResourceTemplate ()
	{
		I2cSerialBus (STARLABS_TOUCHPAD_I2C_ADDR, ControllerInitiated, 400000,
			AddressingMode7Bit, "\\_SB.PCI0.I2C0")
	})
	OperationRegion (TPOR, GenericSerialBus, Zero, 0x100)
	Field (TPOR, BufferAcc, NoLock, Preserve)
	{
		Connection (TCON),
		AccessAs (BufferAcc, AttribRawBytes (29)),
		TWRT, 8,
		AccessAs (BufferAcc, AttribRawProcessBytes (23)),
		TGET, 8
	}

	/* Keep the CST read selector and its GET_REPORT in one serialized method. */
	Method (TCMD, 3, Serialized)
	{
		Local0 = Buffer (CST_WRITE_BUFFER)
		{
			0, CST_WRITE_LENGTH,
			STARLABS_TOUCHPAD_FALLBACK_CMD_REG, 0,
			I2C_HID_FEATURE_REPORT (STARLABS_TOUCHPAD_CST_REPORT_ID),
			I2C_HID_OPCODE_SET_REPORT,
			STARLABS_TOUCHPAD_FALLBACK_DATA_REG, 0,
			CST_HID_LENGTH, 0, STARLABS_TOUCHPAD_CST_REPORT_ID
		}
		Local0[CST_COMMAND_INDEX] = Arg0
		Local0[CST_COMMAND_INDEX + 1] = Arg1 & 0xff
		Local0[CST_COMMAND_INDEX + 2] = Arg1 >> 8
		Local0[CST_COMMAND_INDEX + 3] = Arg2 & 0xff
		Local0[CST_COMMAND_INDEX + 4] = Arg2 >> 8
		If (Arg0 == STARLABS_TOUCHPAD_CST_WRITE_HAPTICS)
		{
			Local0[CST_COMMAND_INDEX + 2] = Arg1 & 0xff
		}
		ElseIf (Arg0 == STARLABS_TOUCHPAD_CST_WRITE_FORCE)
		{
			Local0[CST_COMMAND_INDEX + 5] =
				(Arg1 ^ (Arg1 >> 8) ^ Arg2 ^ (Arg2 >> 8)) & 0xff
		}
		Local0 = (TWRT = Local0)
		If (CST_BYTE (Local0, 0) || (Arg0 & 0x80))
		{
			Return (Local0)
		}

		Local0 = Buffer (8)
		{
			0, 6, STARLABS_TOUCHPAD_FALLBACK_CMD_REG, 0,
			I2C_HID_FEATURE_REPORT (STARLABS_TOUCHPAD_CST_REPORT_ID),
			I2C_HID_OPCODE_GET_REPORT,
			STARLABS_TOUCHPAD_FALLBACK_DATA_REG, 0
		}
		Local0 = (TGET = Local0)
		If ((CST_BYTE (Local0, 1) != CST_HID_LENGTH) ||
		    (CST_WORD (Local0, 2) != CST_HID_LENGTH) ||
		    (CST_BYTE (Local0, 4) != STARLABS_TOUCHPAD_CST_REPORT_ID))
		{
			Local0[0] = STARLABS_CFR_RUNTIME_ERROR
		}
		Return (Local0)
	}

	Method (VFRC, 2, NotSerialized)
	{
		If (Arg1 == STARLABS_CFR_ID_TOUCHPAD_FORCE_RELEASE)
		{
			Return ((Arg0 == STARLABS_TOUCHPAD_RELEASE_FORCE_MINIMAL) ||
				(Arg0 == STARLABS_TOUCHPAD_RELEASE_FORCE_LOW) ||
				(Arg0 == STARLABS_TOUCHPAD_RELEASE_FORCE_AVERAGE) ||
				(Arg0 == STARLABS_TOUCHPAD_RELEASE_FORCE_HIGH) ||
				(Arg0 == STARLABS_TOUCHPAD_RELEASE_FORCE_HULK))
		}
		Return ((Arg0 == STARLABS_TOUCHPAD_PRESS_FORCE_MINIMAL) ||
			(Arg0 == STARLABS_TOUCHPAD_PRESS_FORCE_LOW) ||
			(Arg0 == STARLABS_TOUCHPAD_PRESS_FORCE_AVERAGE) ||
			(Arg0 == STARLABS_TOUCHPAD_PRESS_FORCE_HIGH) ||
			(Arg0 == STARLABS_TOUCHPAD_PRESS_FORCE_HULK))
	}

	Method (TPGT, 1, Serialized)
	{
		Local1 = Package (2) { STARLABS_CFR_RUNTIME_ERROR, 0 }
		If (Arg0 == STARLABS_CFR_ID_TOUCHPAD_HAPTICS)
		{
			Local0 = TCMD (STARLABS_TOUCHPAD_CST_READ_HAPTICS, 0, 0)
			If (!CST_BYTE (Local0, 0))
			{
				Local1[0] = STARLABS_CFR_RUNTIME_SUCCESS
				Local1[1] = CST_BYTE (Local0, CST_PAYLOAD_INDEX)
			}
		}
		ElseIf ((Arg0 == STARLABS_CFR_ID_TOUCHPAD_FORCE_PRESS) ||
			(Arg0 == STARLABS_CFR_ID_TOUCHPAD_FORCE_RELEASE))
		{
			Local0 = TCMD (STARLABS_TOUCHPAD_CST_READ_FORCE, 0, 0)
			If (!CST_BYTE (Local0, 0))
			{
				Local1[0] = STARLABS_CFR_RUNTIME_SUCCESS
				Local2 = CST_PAYLOAD_INDEX
				If (Arg0 == STARLABS_CFR_ID_TOUCHPAD_FORCE_RELEASE)
				{
					Local2 += 2
				}
				Local1[1] = CST_WORD (Local0, Local2)
			}
		}
		Return (Local1)
	}

	Method (TPAP, 2, Serialized)
	{
		If (Arg0 == STARLABS_CFR_ID_TOUCHPAD_HAPTICS)
		{
			If ((Arg1 != STARLABS_TOUCHPAD_HAPTICS_LOW) &&
			    (Arg1 != STARLABS_TOUCHPAD_HAPTICS_MEDIUM) &&
			    (Arg1 != STARLABS_TOUCHPAD_HAPTICS_HIGH) &&
			    (Arg1 != STARLABS_TOUCHPAD_HAPTICS_DEFAULT) &&
			    (Arg1 != STARLABS_TOUCHPAD_HAPTICS_MAX))
			{
				Return (STARLABS_CFR_RUNTIME_ERROR)
			}
			Local1 = STARLABS_TOUCHPAD_CST_WRITE_HAPTICS
			Local2 = Arg1
			Local3 = 0
		}
		ElseIf ((Arg0 == STARLABS_CFR_ID_TOUCHPAD_FORCE_PRESS) ||
			(Arg0 == STARLABS_CFR_ID_TOUCHPAD_FORCE_RELEASE))
		{
			If (!VFRC (Arg1, Arg0))
			{
				Return (STARLABS_CFR_RUNTIME_ERROR)
			}
			Local0 = TCMD (STARLABS_TOUCHPAD_CST_READ_FORCE, 0, 0)
			If (CST_BYTE (Local0, 0))
			{
				Return (STARLABS_CFR_RUNTIME_ERROR)
			}
			Local1 = STARLABS_TOUCHPAD_CST_WRITE_FORCE
			Local2 = CST_WORD (Local0, CST_PAYLOAD_INDEX)
			Local3 = CST_WORD (Local0, CST_PAYLOAD_INDEX + 2)
			If (Arg0 == STARLABS_CFR_ID_TOUCHPAD_FORCE_PRESS)
			{
				Local2 = Arg1
			}
			Else
			{
				Local3 = Arg1
			}
		}
		Else
		{
			Return (STARLABS_CFR_RUNTIME_ERROR)
		}

		Local4 = 0
		While (Local4 < STARLABS_TOUCHPAD_RETRIES)
		{
			Local0 = TCMD (Local1, Local2, Local3)
			If (!CST_BYTE (Local0, 0))
			{
				Local5 = 0
				While (Local5 < STARLABS_TOUCHPAD_VERIFY_RETRIES)
				{
					Local0 = TPGT (Arg0)
					If (!DerefOf (Local0[0]) && (DerefOf (Local0[1]) == Arg1))
					{
						Return (STARLABS_CFR_RUNTIME_SUCCESS)
					}
					Sleep (STARLABS_TOUCHPAD_VERIFY_DELAY_MS)
					Local5++
				}
			}
			Sleep (STARLABS_TOUCHPAD_RETRY_DELAY_MS)
			Local4++
		}
		Return (STARLABS_CFR_RUNTIME_ERROR)
	}
}
