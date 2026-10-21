/* SPDX-License-Identifier: GPL-2.0-only */
#include <intelblocks/smm_gpio_defs.h>

#if CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_GPIO)
OperationRegion (APMP, SystemIO, SMM_GPIO_APM_PORT, 1)
Field (APMP, ByteAcc, NoLock, Preserve)
{
	APMC, 8,
}

Mutex (GPMX, 0)

Method (PGTX, 1, Serialized)
{
	If (Acquire (GPMX, 0xffff))
	{
		/* Legacy GET has no error channel; its documented fallback is raw low. */
		Return (Zero)
	}
	GPAD = Arg0
	GPOP = SMM_GPIO_GET
	GPVL = 0
	GPST = 1
	APMC = APM_CNT_GPIO
	Local0 = GPST
	Local1 = GPVL
	Release (GPMX)
	/* Only a successful single-bit response is valid for the legacy GET API. */
	If (Local0 || Local1 > One)
	{
		Return (Zero)
	}
	Return (Local1)
}

Method (PSTX, 1, Serialized)
{
	If (Acquire (GPMX, 0xffff))
	{
		Return (1)
	}
	GPAD = Arg0
	GPOP = SMM_GPIO_SET
	GPST = 1
	APMC = APM_CNT_GPIO
	Local0 = GPST
	Release (GPMX)
	Return (Local0)
}

Method (PCTX, 1, Serialized)
{
	If (Acquire (GPMX, 0xffff))
	{
		Return (1)
	}
	GPAD = Arg0
	GPOP = SMM_GPIO_CLEAR
	GPST = 1
	APMC = APM_CNT_GPIO
	Local0 = GPST
	Release (GPMX)
	Return (Local0)
}
#endif

/*
 * Get GPIO Value
 * Arg0 - GPIO Number
 */
Method (GRXS, 1, Serialized)
{
	OperationRegion (PREG, SystemMemory, GADD (Arg0), 4)
	Field (PREG, AnyAcc, NoLock, Preserve)
	{
		,     1,
		RXST, 1,
	}

	Return (RXST)
}

/*
 * Get GPIO Tx Value
 * Arg0 - GPIO Number
 */
Method (GTXS, 1, Serialized)
{
	OperationRegion (PREG, SystemMemory, GADD (Arg0), 4)
	Field (PREG, AnyAcc, NoLock, Preserve)
	{
		TXST, 1,
	}

	Return (TXST)
}

/*
 * Set GPIO Tx Value
 * Arg0 - GPIO Number
 */
Method (STXS, 1, Serialized)
{
	OperationRegion (PREG, SystemMemory, GADD (Arg0), 4)
	Field (PREG, AnyAcc, NoLock, Preserve)
	{
		TXST, 1,
	}
	TXST = 1
}

/*
 * Clear GPIO Tx Value
 * Arg0 - GPIO Number
 */
Method (CTXS, 1, Serialized)
{
	OperationRegion (PREG, SystemMemory, GADD (Arg0), 4)
	Field (PREG, AnyAcc, NoLock, Preserve)
	{
		TXST, 1,
	}
	TXST = 0
}

/*
 * Set Pad mode
 * Arg0 - GPIO Number
 * Arg1 - Pad mode
 *     0 = GPIO control pad
 *     1 = Native Function 1
 *     2 = Native Function 2
 *     3 = Native Function 3
 */
Method (GPMO, 2, Serialized)
{
	OperationRegion (PREG, SystemMemory, GADD (Arg0), 4)
	Field (PREG, AnyAcc, NoLock, Preserve)
	{
		,     10,
		MODE, 3,
	}
	MODE = Arg1
}

/*
 * Enable/Disable Tx buffer
 * Arg0 - GPIO Number
 * Arg1 - TxBuffer state
 *     0 = Disable Tx Buffer
 *     1 = Enable Tx Buffer
 */
Method (GTXE, 2, Serialized)
{
	OperationRegion (PREG, SystemMemory, GADD (Arg0), 4)
	Field (PREG, AnyAcc, NoLock, Preserve)
	{
		,     8,
		TXDI, 1,
	}

	TXDI = !Arg1
}

/*
 * Enable/Disable Rx buffer
 * Arg0 - GPIO Number
 * Arg1 - RxBuffer state
 *     0 = Disable Rx Buffer
 *     1 = Enable Rx Buffer
 */
Method (GRXE, 2, Serialized)
{
	OperationRegion (PREG, SystemMemory, GADD (Arg0), 4)
	Field (PREG, AnyAcc, NoLock, Preserve)
	{
		,     9,
		RXDI, 1,
	}

	RXDI = !Arg1
}

/*
 * Enable/Disable SCI interrupt route
 * Arg0 - GPIO Number
 * Arg1 - Route to SCI#
 *     0 = Disable IRQ route to SCI#
 *     1 = Enable IRQ route to SCI#
 */
Method (GSCI, 2, Serialized)
{
	OperationRegion (PREG, SystemMemory, GADD (Arg0), 4)
	Field (PREG, AnyAcc, NoLock, Preserve)
	{
		,     19,
		SCIR, 1,
	}

	SCIR = Arg1
}
