/* SPDX-License-Identifier: GPL-2.0-only */

#include "opal_nvme.h"
#include "opal_secure.h"
#include "opal_unlock.h"

#include "tcg_storage_core_lib.h"

#include <console/console.h>
#include <cpu/x86/smm.h>
#include <commonlib/bsd/helpers.h>
#include <delay.h>
#include <string.h>
#include <types.h>

#define TCG_OPAL_SECURITY_PROTOCOL_1 0x01

/* Minimal Opal UIDs needed for S3 unlock. */
#define OPAL_UID_LOCKING_SP			TCG_TO_UID(0x00, 0x00, 0x02, 0x05, 0x00, 0x00, 0x00, 0x02)
#define OPAL_LOCKING_SP_ADMIN1_AUTHORITY	TCG_TO_UID(0x00, 0x00, 0x00, 0x09, 0x00, 0x01, 0x00, 0x01)
#define OPAL_LOCKING_SP_USER1_AUTHORITY	TCG_TO_UID(0x00, 0x00, 0x00, 0x09, 0x00, 0x03, 0x00, 0x01)
#define OPAL_LOCKING_SP_LOCKING_GLOBALRANGE	TCG_TO_UID(0x00, 0x00, 0x08, 0x02, 0x00, 0x00, 0x00, 0x01)

#define TRUSTED_COMMAND_TIMEOUT_NS ((u64)5 * 1000 * 1000 * 1000ULL)

typedef struct {
	UINT32 HostSessionId;
	UINT32 TperSessionId;
	UINT16 ComIdExtension;
	UINT16 OpalBaseComId;
	struct opal_nvme *Nvme;
} OPAL_SESSION;

static TCG_RESULT opal_trusted_send(OPAL_SESSION *Session, UINT8 SecurityProtocol,
				    UINT16 SpSpecific, UINTN TransferLength,
				    VOID *Buffer, UINTN BufferSize)
{
	const UINTN TransferLength512 = (TransferLength + 511) & ~(UINTN)511;

	if (TransferLength512 > BufferSize)
		return TcgResultFailureBufferTooSmall;

	ZeroMem((UINT8 *)Buffer + TransferLength, TransferLength512 - TransferLength);

	if (opal_nvme_security_send(Session->Nvme, SecurityProtocol, SpSpecific,
				   Buffer, TransferLength512))
		return TcgResultFailure;

	return TcgResultSuccess;
}

static TCG_RESULT opal_trusted_recv(OPAL_SESSION *Session, UINT8 SecurityProtocol,
				    UINT16 SpSpecific, VOID *Buffer, UINTN BufferSize,
				    UINT32 EstimateTimeCost)
{
	UINTN TransferLength512 = BufferSize & ~(UINTN)511;
	UINT32 Tries;
	UINT32 Length;
	UINT32 OutstandingData;
	TCG_COM_PACKET *ComPacket;

	if (TransferLength512 < sizeof(TCG_COM_PACKET))
		return TcgResultFailureBufferTooSmall;

	Tries = (EstimateTimeCost > 10) ? (EstimateTimeCost * 500) : 5000;

	while (Tries-- > 0) {
		size_t TransferSize = 0;

		ZeroMem(Buffer, BufferSize);
		if (opal_nvme_security_recv(Session->Nvme, SecurityProtocol, SpSpecific,
					    Buffer, TransferLength512, &TransferSize))
			return TcgResultFailure;

		if (SecurityProtocol != TCG_OPAL_SECURITY_PROTOCOL_1)
			return TcgResultSuccess;

		ComPacket = (TCG_COM_PACKET *)Buffer;
		Length = SwapBytes32(ComPacket->LengthBE);
		OutstandingData = SwapBytes32(ComPacket->OutstandingDataBE);

		if ((Length != 0) && (OutstandingData == 0))
			return TcgResultSuccess;

		udelay(2000);
	}

	return TcgResultFailure;
}

static TCG_RESULT opal_perform_method(OPAL_SESSION *Session, UINT32 SendSize, VOID *Buffer,
				      UINT32 BufferSize, TCG_PARSE_STRUCT *ParseStruct,
				      UINT8 *MethodStatus, UINT32 EstimateTimeCost)
{
	NULL_CHECK(Session);
	NULL_CHECK(MethodStatus);

	ERROR_CHECK(opal_trusted_send(Session, TCG_OPAL_SECURITY_PROTOCOL_1, Session->OpalBaseComId,
				      SendSize, Buffer, BufferSize));
	ERROR_CHECK(opal_trusted_recv(Session, TCG_OPAL_SECURITY_PROTOCOL_1, Session->OpalBaseComId,
				      Buffer, BufferSize, EstimateTimeCost));

	ERROR_CHECK(TcgInitTcgParseStruct(ParseStruct, Buffer, BufferSize));
	ERROR_CHECK(TcgCheckComIds(ParseStruct, Session->OpalBaseComId, Session->ComIdExtension));
	ERROR_CHECK(TcgGetMethodStatus(ParseStruct, MethodStatus));

	return TcgResultSuccess;
}

static TCG_RESULT opal_start_session(OPAL_SESSION *Session, TCG_UID SpId, BOOLEAN Write,
				     UINT32 HostChallengeLength, const VOID *HostChallenge,
				     TCG_UID HostSigningAuthority, VOID *Buffer, UINT32 BufferSize,
				     UINT8 *MethodStatus)
{
	TCG_CREATE_STRUCT CreateStruct;
	TCG_PARSE_STRUCT ParseStruct;
	UINT32 Size;
	UINT16 ComIdExtension = 0;
	UINT32 HostSessionId = 1;
	TCG_RESULT Ret = TcgResultSuccess;

	if (!Session || !MethodStatus || !Buffer)
		return TcgResultFailureNullPointer;

	if (BufferSize < 512)
		return TcgResultFailureBufferTooSmall;

	*MethodStatus = 0;

	Session->ComIdExtension = ComIdExtension;
	Session->HostSessionId = HostSessionId;

	Ret = TcgInitTcgCreateStruct(&CreateStruct, Buffer, BufferSize);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = TcgCreateStartSession(&CreateStruct, &Size, Session->OpalBaseComId,
				    ComIdExtension, HostSessionId, SpId, Write,
				    HostChallengeLength, HostChallenge, HostSigningAuthority);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = opal_perform_method(Session, Size, Buffer, BufferSize, &ParseStruct, MethodStatus, 0);
	if (Ret != TcgResultSuccess)
		goto out;
	if (*MethodStatus != TCG_METHOD_STATUS_CODE_SUCCESS)
		goto out;

	if (TcgParseSyncSession(&ParseStruct, Session->OpalBaseComId, ComIdExtension, HostSessionId,
				&Session->TperSessionId) != TcgResultSuccess) {
		Ret = TcgResultFailure;
		goto out;
	}

out:
	opal_explicit_bzero(Buffer, BufferSize);
	return Ret;
}

static TCG_RESULT opal_end_session(OPAL_SESSION *Session, VOID *Buffer, UINT32 BufferSize)
{
	TCG_CREATE_STRUCT CreateStruct;
	UINT32 Size;
	TCG_PARSE_STRUCT ParseStruct;
	TCG_RESULT Ret = TcgResultSuccess;

	if (!Session || !Buffer)
		return TcgResultFailureNullPointer;

	if (BufferSize < 512)
		return TcgResultFailureBufferTooSmall;

	Ret = TcgInitTcgCreateStruct(&CreateStruct, Buffer, BufferSize);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = TcgCreateEndSession(&CreateStruct, &Size, Session->OpalBaseComId,
				  Session->ComIdExtension, Session->HostSessionId,
				  Session->TperSessionId);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = opal_trusted_send(Session, TCG_OPAL_SECURITY_PROTOCOL_1, Session->OpalBaseComId,
				Size, Buffer, BufferSize);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = opal_trusted_recv(Session, TCG_OPAL_SECURITY_PROTOCOL_1, Session->OpalBaseComId,
				Buffer, BufferSize, 0);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = TcgInitTcgParseStruct(&ParseStruct, Buffer, BufferSize);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = TcgCheckComIds(&ParseStruct, Session->OpalBaseComId, Session->ComIdExtension);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = TcgGetNextEndOfSession(&ParseStruct);
	if (Ret != TcgResultSuccess)
		goto out;

out:
	opal_explicit_bzero(Buffer, BufferSize);
	return Ret;
}

static TCG_RESULT opal_update_global_locking_range(OPAL_SESSION *Session,
						   BOOLEAN ReadLocked, BOOLEAN WriteLocked,
						   VOID *Buffer, UINT32 BufferSize,
						   UINT8 *MethodStatus)
{
	TCG_CREATE_STRUCT CreateStruct;
	TCG_PARSE_STRUCT ParseStruct;
	UINT32 Size;
	TCG_RESULT Ret = TcgResultSuccess;

	if (!Session || !MethodStatus || !Buffer)
		return TcgResultFailureNullPointer;

	if (BufferSize < 512)
		return TcgResultFailureBufferTooSmall;

	Ret = TcgInitTcgCreateStruct(&CreateStruct, Buffer, BufferSize);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = TcgStartComPacket(&CreateStruct, Session->OpalBaseComId, Session->ComIdExtension);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgStartPacket(&CreateStruct, Session->TperSessionId, Session->HostSessionId, 0, 0, 0);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgStartSubPacket(&CreateStruct, 0);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgStartMethodCall(&CreateStruct, OPAL_LOCKING_SP_LOCKING_GLOBALRANGE, TCG_UID_METHOD_SET);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgStartParameters(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = TcgAddStartName(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgAddUINT8(&CreateStruct, 0x01); /* "Values" */
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgAddStartList(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = TcgAddStartName(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgAddUINT8(&CreateStruct, 0x07); /* "ReadLocked" */
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgAddBOOLEAN(&CreateStruct, ReadLocked);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgAddEndName(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = TcgAddStartName(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgAddUINT8(&CreateStruct, 0x08); /* "WriteLocked" */
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgAddBOOLEAN(&CreateStruct, WriteLocked);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgAddEndName(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = TcgAddEndList(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgAddEndName(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgEndParameters(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgEndMethodCall(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgEndSubPacket(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgEndPacket(&CreateStruct);
	if (Ret != TcgResultSuccess)
		goto out;
	Ret = TcgEndComPacket(&CreateStruct, &Size);
	if (Ret != TcgResultSuccess)
		goto out;

	Ret = opal_perform_method(Session, Size, Buffer, BufferSize, &ParseStruct, MethodStatus, 0);
	if (Ret != TcgResultSuccess)
		goto out;
	if (*MethodStatus != TCG_METHOD_STATUS_CODE_SUCCESS) {
		Ret = TcgResultSuccess;
		goto out;
	}

out:
	opal_explicit_bzero(Buffer, BufferSize);
	return Ret;
}

static TCG_RESULT opal_util_update_global_locking_range(OPAL_SESSION *Session, const VOID *Password,
							UINT32 PasswordLength, BOOLEAN ReadLocked,
							BOOLEAN WriteLocked, VOID *Buffer,
							UINT32 BufferSize)
{
	UINT8 MethodStatus = 0;
	TCG_RESULT Ret;

	NULL_CHECK(Session);
	NULL_CHECK(Password);

	/* Try admin1 authority. */
	Ret = opal_start_session(Session, OPAL_UID_LOCKING_SP, TRUE, PasswordLength, Password,
				 OPAL_LOCKING_SP_ADMIN1_AUTHORITY, Buffer, BufferSize,
				 &MethodStatus);
	if ((Ret == TcgResultSuccess) && (MethodStatus == TCG_METHOD_STATUS_CODE_SUCCESS)) {
		Ret = opal_update_global_locking_range(Session, ReadLocked, WriteLocked, Buffer,
						       BufferSize, &MethodStatus);
		opal_end_session(Session, Buffer, BufferSize);
		if ((Ret == TcgResultSuccess) && (MethodStatus == TCG_METHOD_STATUS_CODE_SUCCESS))
			return TcgResultSuccess;
	}

	/* Try user1 authority. */
	Ret = opal_start_session(Session, OPAL_UID_LOCKING_SP, TRUE, PasswordLength, Password,
				 OPAL_LOCKING_SP_USER1_AUTHORITY, Buffer, BufferSize,
				 &MethodStatus);
	if (Ret != TcgResultSuccess)
		goto done;
	if (MethodStatus != TCG_METHOD_STATUS_CODE_SUCCESS)
		goto done;

	Ret = opal_update_global_locking_range(Session, ReadLocked, WriteLocked, Buffer, BufferSize,
					       &MethodStatus);
	opal_end_session(Session, Buffer, BufferSize);

done:
	if ((Ret == TcgResultSuccess) && (MethodStatus != TCG_METHOD_STATUS_CODE_SUCCESS)) {
		if (MethodStatus == TCG_METHOD_STATUS_CODE_AUTHORITY_LOCKED_OUT)
			Ret = TcgResultFailureInvalidType;
		else
			Ret = TcgResultFailure;
	}

	return Ret;
}

u32 opal_nvme_opal_unlock(pci_devfn_t dev, u16 base_comid, const u8 *password,
			  u8 password_len, void *scratch, size_t scratch_size)
{
	struct opal_nvme nvme;
	OPAL_SESSION Session;
	TCG_RESULT Ret;
	u8 *io_buf;
	u32 rc = 0;
	u8 *queue_base;

	if (!password || !password_len) {
		printk(BIOS_ERR, "OPAL: missing password\n");
		return 4;
	}

	if (!scratch || scratch_size < (3 * 4096)) {
		printk(BIOS_ERR, "OPAL: invalid scratch buffer\n");
		return 2;
	}

	if (smm_points_to_smram(scratch, scratch_size)) {
		printk(BIOS_ERR, "OPAL: scratch overlaps SMRAM\n");
		return 2;
	}

	queue_base = (u8 *)ALIGN_UP((uintptr_t)scratch, 4096);
	if ((uintptr_t)queue_base + (3 * 4096) > (uintptr_t)scratch + scratch_size) {
		printk(BIOS_ERR, "OPAL: scratch alignment overflow\n");
		rc = 2;
		goto out_early;
	}

	/* Use a 4KiB IO buffer after the queue pages. */
	io_buf = queue_base + (2 * 4096);

	if (opal_nvme_init(&nvme, dev, scratch, scratch_size)) {
		/* Fail closed and ensure stale DMA-visible buffers are cleared. */
		opal_explicit_bzero(queue_base, 3 * 4096);
		return 1;
	}

	memset(&Session, 0, sizeof(Session));
	Session.Nvme = &nvme;
	Session.OpalBaseComId = base_comid;
	Session.ComIdExtension = 0;

	/* Best-effort unlock. */
	Ret = opal_util_update_global_locking_range(&Session, password, password_len, FALSE, FALSE,
						    io_buf, 4096);

	opal_explicit_bzero(io_buf, 4096);

	/* Clear DMA-visible admin queues as well. */
	opal_explicit_bzero(queue_base, 2 * 4096);
	opal_nvme_deinit(&nvme);

	rc = (Ret == TcgResultSuccess) ? 0 : 3;
	return rc;
out_early:
	{
		const uintptr_t end = (uintptr_t)scratch + scratch_size;
		const uintptr_t start = (uintptr_t)queue_base;
		if (end > start) {
			size_t safe = end - start;
			if (safe > (3 * 4096))
				safe = 3 * 4096;
			opal_explicit_bzero(queue_base, safe);
		}
	}
	return rc;
}
