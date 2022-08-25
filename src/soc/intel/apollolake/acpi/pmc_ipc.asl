/* SPDX-License-Identifier: GPL-2.0-only */

#include <soc/iomap.h>

Scope (\_SB.PCI0)
{
	OperationRegion(IPCR, SystemMemory, PMC_BAR1, 0xa0)
	Field (IPCR, DWordAcc, NoLock, Preserve)
	{
		Offset (0x00),  /* Command Register	*/
		ICMD, 32,	/* Command		*/

		Offset (0x04),	/* Status Register	*/
		IRDY, 1,	/* Ready/Busy		*/
		IERR, 1,	/* Error Flag		*/
		    , 14,	/* Reserved		*/
		IERC, 8,	/* Error Code		*/

		Offset (0x80),  /* Write Buffer		*/
		IWB0, 32,
		IWB1, 32,

		Offset (0x90),  /* Read Buffer		*/
		IRB0, 32
	}

	/*
	 * IPC1 Access Method
	 * A way to access the IPC1 via the Trusted Sideband Tunnel.
	 *
	 * Input:
	 *	Arg0:	Command ID
	 *	Arg1:	Sub Command ID
	 *	Arg2:	Size
	 *
	 * Output:
	 *	Error Code
	 */

	Method (IPCM, 3, Serialized)
	{
		/* Create IPC_CMD Register */
		Local0 = 0
		Or (ShiftLeft (And (Arg0, 0xff),  0), Local0, Local0)	/*  7:0  Command ID	*/
		Or (ShiftLeft (And (Arg1, 0x0f), 12), Local0, Local0)	/* 15:12 Sub Command ID */
		Or (ShiftLeft (And (Arg2, 0xff), 16), Local0, Local0)	/* 23:16 Size		*/

		/* Wait for IPC1 to be ready */
		While (IRDY) {
			Sleep (1)
		}

		/* Write IPC_CMD (triggers IPC) */
		ICMD = Local0

		/* Wait for IPC1 to complete */
		While (IRDY) {
			Sleep (1)
		}

		/* Return Error Code */
		Return (IERC)
	}

	/*
	 * Trusted Sideband Tunnel Method
	 *
	 * Input:
	 *	Arg0:	Address
	 *	Arg1:	Data
	 *	Arg2:	Read/Write
	 *
	 * Output:
	 *	Read Data
	 */

	Method (TSTM, 3, Serialized)
	{
		Name (TSTC, 0xe4)

		/* Set Size based on operation */
		If (Arg2 == 0x00) {
			Local0 = 0x01
		} ElseIf (Arg1 = 0x01) {
			Local0 = 0x08
		} Else {
			Return (0x00)
		}

		/* Store address token in Write Buffer */
		Switch (ToInteger(Arg0)) {
			/*
			 * xecp_supp_usb2_2	= 0xa28008
			 * tx_data_dly_1	= 0x824
			 * normalintrstsena	= 0x34
			 * auto_tuning		= 0x840
			 * rx_cmd_dly_2		= 0x834
			 */
			Case (0xa28008)
			{
				IWB0 = 0x05
			}
			Case (0x824)
			{
				IWB0 = 0x04
			}
			Case (0x34)
			{
				IWB0 = 0x03
			}
			Case (0x840)
			{
				IWB0 = 0x02
			}
			Case (0x834)
			{
				IWB0 = 0x01
			}
			Default
			{
				IWB0 = 0x00
			}
		}

		/* Store data in Write Buffer */
		IWB1 = Arg1

		/* Issue IPC1 */
		IPCM (TSTC, Arg2, Local0)

		/* Return Read Buffer */
		Return (IRB0)
	}
}
