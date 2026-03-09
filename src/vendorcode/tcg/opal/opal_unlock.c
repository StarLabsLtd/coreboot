/* SPDX-License-Identifier: GPL-2.0-only */

#include "opal_unlock.h"

#include <vendorcode/intel/tcg_storage_core/tcg_storage_core_lib.h>

#include <console/console.h>
#include <device/pci_def.h>
#include <cpu/x86/smm.h>
#include <commonlib/bsd/helpers.h>
#include <delay.h>
#include <security/tcg/opal_s3/opal_nvme.h>
#include <security/tcg/opal_s3/opal_secure.h>
#include <string.h>
#include <types.h>

#define TCG_OPAL_SECURITY_PROTOCOL_1 0x01

/* Minimal Opal UIDs needed for S3 unlock. */
#define OPAL_UID_LOCKING_SP TCG_TO_UID(0x00, 0x00, 0x02, 0x05, 0x00, 0x00, 0x00, 0x02)
#define OPAL_LOCKING_SP_ADMIN1_AUTHORITY \
	TCG_TO_UID(0x00, 0x00, 0x00, 0x09, 0x00, 0x01, 0x00, 0x01)
#define OPAL_LOCKING_SP_USER1_AUTHORITY \
	TCG_TO_UID(0x00, 0x00, 0x00, 0x09, 0x00, 0x03, 0x00, 0x01)
#define OPAL_LOCKING_SP_LOCKING_GLOBALRANGE \
	TCG_TO_UID(0x00, 0x00, 0x08, 0x02, 0x00, 0x00, 0x00, 0x01)

#define TRUSTED_COMMAND_TIMEOUT_NS ((u64)5 * 1000 * 1000 * 1000ULL)

#define OPAL_NVME_PAGE_SIZE         4096U
#define OPAL_NVME_QUEUE_PAGES       2U
#define OPAL_NVME_SCRATCH_PAGES     3U
#define OPAL_NVME_QUEUE_BYTES       (OPAL_NVME_QUEUE_PAGES * OPAL_NVME_PAGE_SIZE)
#define OPAL_NVME_SCRATCH_MIN_BYTES (OPAL_NVME_SCRATCH_PAGES * OPAL_NVME_PAGE_SIZE)

typedef struct {
	UINT32 HostSessionId;
	UINT32 TperSessionId;
	UINT16 ComIdExtension;
	UINT16 OpalBaseComId;
	struct opal_nvme *Nvme;
} OPAL_SESSION;

static const char *tcg_result_string(TCG_RESULT ret)
{
	switch (ret) {
	case TcgResultSuccess:
		return "Success";
	case TcgResultFailure:
		return "Failure";
	case TcgResultFailureNullPointer:
		return "FailureNullPointer";
	case TcgResultFailureZeroSize:
		return "FailureZeroSize";
	case TcgResultFailureInvalidAction:
		return "FailureInvalidAction";
	case TcgResultFailureBufferTooSmall:
		return "FailureBufferTooSmall";
	case TcgResultFailureEndBuffer:
		return "FailureEndBuffer";
	case TcgResultFailureInvalidType:
		return "FailureInvalidType";
	}

	return "Unknown";
}

static const char *opal_authority_string(TCG_UID authority)
{
	switch (authority) {
	case OPAL_LOCKING_SP_ADMIN1_AUTHORITY:
		return "Admin1";
	case OPAL_LOCKING_SP_USER1_AUTHORITY:
		return "User1";
	default:
		return "Unknown";
	}
}

static TCG_RESULT opal_trusted_send(OPAL_SESSION *Session, UINT8 SecurityProtocol,
				    UINT16 SpSpecific, UINTN TransferLength, VOID *Buffer,
				    UINTN BufferSize)
{
	const UINTN TransferLength512 = (TransferLength + 511) & ~(UINTN)511;

	if (TransferLength512 > BufferSize) {
		printk(BIOS_ERR,
		       "OPAL: trusted send buffer too small sp=0x%02x sps=0x%04x xfer=0x%zx padded=0x%zx buf=0x%zx\n",
		       SecurityProtocol, SpSpecific, TransferLength, TransferLength512,
		       BufferSize);
		return TcgResultFailureBufferTooSmall;
	}

	ZeroMem((UINT8 *)Buffer + TransferLength, TransferLength512 - TransferLength);
	printk(BIOS_DEBUG,
	       "OPAL: trusted send sp=0x%02x sps=0x%04x xfer=0x%zx padded=0x%zx\n",
	       SecurityProtocol, SpSpecific, TransferLength, TransferLength512);

	if (opal_nvme_security_send(Session->Nvme, SecurityProtocol, SpSpecific, Buffer,
				    TransferLength512)) {
		printk(BIOS_ERR,
		       "OPAL: trusted send failed sp=0x%02x sps=0x%04x xfer=0x%zx\n",
		       SecurityProtocol, SpSpecific, TransferLength512);
		return TcgResultFailure;
	}

	return TcgResultSuccess;
}

static TCG_RESULT opal_trusted_recv(OPAL_SESSION *Session, UINT8 SecurityProtocol,
				    UINT16 SpSpecific, VOID *Buffer, UINTN BufferSize,
				    UINT32 EstimateTimeCost)
{
	UINTN TransferLength512 = BufferSize & ~(UINTN)511;
	UINT32 TotalTries;
	UINT32 Tries;
	UINT32 LastLength = 0;
	UINT32 LastOutstandingData = 0;
	size_t LastTransferSize = 0;
	UINT32 Length;
	UINT32 OutstandingData;
	TCG_COM_PACKET *ComPacket;

	if (TransferLength512 < sizeof(TCG_COM_PACKET)) {
		printk(BIOS_ERR,
		       "OPAL: trusted recv buffer too small sp=0x%02x sps=0x%04x buf=0x%zx aligned=0x%zx\n",
		       SecurityProtocol, SpSpecific, BufferSize, TransferLength512);
		return TcgResultFailureBufferTooSmall;
	}

	Tries = (EstimateTimeCost > 10) ? (EstimateTimeCost * 500) : 5000;
	TotalTries = Tries;
	printk(BIOS_DEBUG,
	       "OPAL: trusted recv sp=0x%02x sps=0x%04x buf=0x%zx aligned=0x%zx tries=%u estimate=%u\n",
	       SecurityProtocol, SpSpecific, BufferSize, TransferLength512, Tries,
	       EstimateTimeCost);

	while (Tries-- > 0) {
		size_t TransferSize = 0;

		ZeroMem(Buffer, BufferSize);
		if (opal_nvme_security_recv(Session->Nvme, SecurityProtocol, SpSpecific, Buffer,
					    TransferLength512, &TransferSize)) {
			printk(BIOS_ERR,
			       "OPAL: trusted recv transport failed sp=0x%02x sps=0x%04x tries_left=%u/%u\n",
			       SecurityProtocol, SpSpecific, Tries, TotalTries);
			return TcgResultFailure;
		}
		LastTransferSize = TransferSize;

		if (SecurityProtocol != TCG_OPAL_SECURITY_PROTOCOL_1)
			return TcgResultSuccess;

		ComPacket = (TCG_COM_PACKET *)Buffer;
		Length = SwapBytes32(ComPacket->LengthBE);
		OutstandingData = SwapBytes32(ComPacket->OutstandingDataBE);
		LastLength = Length;
		LastOutstandingData = OutstandingData;

		if ((Length != 0) && (OutstandingData == 0)) {
			printk(BIOS_DEBUG,
			       "OPAL: trusted recv complete len=0x%x outstanding=0x%x xfer=0x%zx polls=%u\n",
			       Length, OutstandingData, TransferSize,
			       TotalTries - Tries);
			return TcgResultSuccess;
		}

		udelay(2000);
	}

	printk(BIOS_ERR,
	       "OPAL: trusted recv timed out sp=0x%02x sps=0x%04x len=0x%x outstanding=0x%x xfer=0x%zx\n",
	       SecurityProtocol, SpSpecific, LastLength, LastOutstandingData,
	       LastTransferSize);
	return TcgResultFailure;
}

static TCG_RESULT opal_perform_method(OPAL_SESSION *Session, UINT32 SendSize, VOID *Buffer,
				      UINT32 BufferSize, TCG_PARSE_STRUCT *ParseStruct,
				      UINT8 *MethodStatus, UINT32 EstimateTimeCost)
{
	TCG_RESULT Ret;

	NULL_CHECK(Session);
	NULL_CHECK(MethodStatus);

	printk(BIOS_DEBUG,
	       "OPAL: perform method comid=0x%04x ext=0x%04x send=0x%x buf=0x%x estimate=%u\n",
	       Session->OpalBaseComId, Session->ComIdExtension, SendSize, BufferSize,
	       EstimateTimeCost);

	Ret = opal_trusted_send(Session, TCG_OPAL_SECURITY_PROTOCOL_1,
				Session->OpalBaseComId, SendSize, Buffer, BufferSize);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: perform method send failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		return Ret;
	}

	Ret = opal_trusted_recv(Session, TCG_OPAL_SECURITY_PROTOCOL_1,
				Session->OpalBaseComId, Buffer, BufferSize,
				EstimateTimeCost);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: perform method recv failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		return Ret;
	}

	Ret = TcgInitTcgParseStruct(ParseStruct, Buffer, BufferSize);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: parse init failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		return Ret;
	}

	Ret = TcgCheckComIds(ParseStruct, Session->OpalBaseComId, Session->ComIdExtension);
	if (Ret != TcgResultSuccess) {
		UINT16 ParseComId = 0;
		UINT16 ParseComIdExtension = 0;
		TCG_RESULT ComIdRet;

		ComIdRet = TcgGetComIds(ParseStruct, &ParseComId, &ParseComIdExtension);
		if (ComIdRet == TcgResultSuccess) {
			printk(BIOS_ERR,
			       "OPAL: comid mismatch expected=0x%04x/0x%04x got=0x%04x/0x%04x\n",
			       Session->OpalBaseComId, Session->ComIdExtension,
			       ParseComId, ParseComIdExtension);
		} else {
			printk(BIOS_ERR,
			       "OPAL: comid check failed ret=%d (%s), unable to read response comid ret=%d (%s)\n",
			       Ret, tcg_result_string(Ret), ComIdRet,
			       tcg_result_string(ComIdRet));
		}
		return Ret;
	}

	Ret = TcgGetMethodStatus(ParseStruct, MethodStatus);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: get method status failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		return Ret;
	}
	printk(BIOS_DEBUG, "OPAL: method status=0x%02x (%s)\n", *MethodStatus,
	       TcgMethodStatusString(*MethodStatus));

	return TcgResultSuccess;
}

static TCG_RESULT opal_start_session(OPAL_SESSION *Session, TCG_UID SpId, BOOLEAN Write,
				     UINT32 HostChallengeLength, const VOID *HostChallenge,
				     TCG_UID HostSigningAuthority, UINT8 *MethodStatus)
{
	TCG_CREATE_STRUCT CreateStruct;
	TCG_PARSE_STRUCT ParseStruct;
	UINT32 Size;
	UINT16 ComIdExtension = 0;
	UINT32 HostSessionId = 1;
	UINT8 Buf[512];
	TCG_RESULT Ret = TcgResultSuccess;

	if (!Session || !MethodStatus)
		return TcgResultFailureNullPointer;

	*MethodStatus = 0;
	printk(BIOS_DEBUG,
	       "OPAL: start session authority=%s uid=0x%016llx sp=0x%016llx write=%u challenge_len=%u comid=0x%04x\n",
	       opal_authority_string(HostSigningAuthority),
	       (unsigned long long)HostSigningAuthority, (unsigned long long)SpId, Write,
	       HostChallengeLength, Session->OpalBaseComId);

	Session->ComIdExtension = ComIdExtension;
	Session->HostSessionId = HostSessionId;

	Ret = TcgInitTcgCreateStruct(&CreateStruct, Buf, sizeof(Buf));
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: start session create init failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = TcgCreateStartSession(&CreateStruct, &Size, Session->OpalBaseComId,
				    ComIdExtension, HostSessionId, SpId, Write,
				    HostChallengeLength, HostChallenge, HostSigningAuthority);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: create start session failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = opal_perform_method(Session, Size, Buf, sizeof(Buf), &ParseStruct, MethodStatus,
				  0);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR,
		       "OPAL: start session method failed authority=%s ret=%d (%s) status=0x%02x (%s)\n",
		       opal_authority_string(HostSigningAuthority), Ret,
		       tcg_result_string(Ret), *MethodStatus,
		       TcgMethodStatusString(*MethodStatus));
		goto out;
	}
	if (*MethodStatus != TCG_METHOD_STATUS_CODE_SUCCESS) {
		printk(BIOS_ERR,
		       "OPAL: start session rejected authority=%s status=0x%02x (%s)\n",
		       opal_authority_string(HostSigningAuthority), *MethodStatus,
		       TcgMethodStatusString(*MethodStatus));
		goto out;
	}

	if (TcgParseSyncSession(&ParseStruct, Session->OpalBaseComId, ComIdExtension,
				HostSessionId, &Session->TperSessionId) != TcgResultSuccess) {
		printk(BIOS_ERR,
		       "OPAL: parse sync session failed authority=%s host_sid=0x%x comid=0x%04x\n",
		       opal_authority_string(HostSigningAuthority), HostSessionId,
		       Session->OpalBaseComId);
		Ret = TcgResultFailure;
		goto out;
	}
	printk(BIOS_DEBUG, "OPAL: session started authority=%s host_sid=0x%x tper_sid=0x%x\n",
	       opal_authority_string(HostSigningAuthority), Session->HostSessionId,
	       Session->TperSessionId);

out:
	opal_explicit_bzero(Buf, sizeof(Buf));
	return Ret;
}

static TCG_RESULT opal_end_session(OPAL_SESSION *Session)
{
	UINT8 Buffer[512];
	TCG_CREATE_STRUCT CreateStruct;
	UINT32 Size;
	TCG_PARSE_STRUCT ParseStruct;
	TCG_RESULT Ret = TcgResultSuccess;

	if (!Session)
		return TcgResultFailureNullPointer;

	Ret = TcgInitTcgCreateStruct(&CreateStruct, Buffer, sizeof(Buffer));
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: end session create init failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = TcgCreateEndSession(&CreateStruct, &Size, Session->OpalBaseComId,
				  Session->ComIdExtension, Session->HostSessionId,
				  Session->TperSessionId);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: create end session failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = opal_trusted_send(Session, TCG_OPAL_SECURITY_PROTOCOL_1, Session->OpalBaseComId,
				Size, Buffer, sizeof(Buffer));
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: end session send failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = opal_trusted_recv(Session, TCG_OPAL_SECURITY_PROTOCOL_1, Session->OpalBaseComId,
				Buffer, sizeof(Buffer), 0);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: end session recv failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = TcgInitTcgParseStruct(&ParseStruct, Buffer, sizeof(Buffer));
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: end session parse init failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = TcgCheckComIds(&ParseStruct, Session->OpalBaseComId, Session->ComIdExtension);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: end session comid check failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = TcgGetNextEndOfSession(&ParseStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: end session parse failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	printk(BIOS_DEBUG, "OPAL: session ended host_sid=0x%x tper_sid=0x%x\n",
	       Session->HostSessionId, Session->TperSessionId);

out:
	opal_explicit_bzero(Buffer, sizeof(Buffer));
	return Ret;
}

static TCG_RESULT opal_update_global_locking_range(OPAL_SESSION *Session, BOOLEAN ReadLocked,
						   BOOLEAN WriteLocked, UINT8 *MethodStatus)
{
	UINT8 Buf[512];
	TCG_CREATE_STRUCT CreateStruct;
	TCG_PARSE_STRUCT ParseStruct;
	UINT32 Size;
	TCG_RESULT Ret = TcgResultSuccess;

	if (!Session || !MethodStatus)
		return TcgResultFailureNullPointer;

	*MethodStatus = 0;
	printk(BIOS_DEBUG,
	       "OPAL: set global range read_locked=%u write_locked=%u host_sid=0x%x tper_sid=0x%x\n",
	       ReadLocked, WriteLocked, Session->HostSessionId, Session->TperSessionId);

	Ret = TcgInitTcgCreateStruct(&CreateStruct, Buf, sizeof(Buf));
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: locking range create init failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = TcgStartComPacket(&CreateStruct, Session->OpalBaseComId, Session->ComIdExtension);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: start com packet failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgStartPacket(&CreateStruct, Session->TperSessionId, Session->HostSessionId, 0,
			     0, 0);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: start packet failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgStartSubPacket(&CreateStruct, 0);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: start subpacket failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgStartMethodCall(&CreateStruct, OPAL_LOCKING_SP_LOCKING_GLOBALRANGE,
				 TCG_UID_METHOD_SET);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: start method call failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgStartParameters(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: start parameters failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = TcgAddStartName(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add values start name failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgAddUINT8(&CreateStruct, 0x01); /* "Values" */
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add values selector failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgAddStartList(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add values list start failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = TcgAddStartName(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add readlocked start name failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgAddUINT8(&CreateStruct, 0x07); /* "ReadLocked" */
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add readlocked selector failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgAddBOOLEAN(&CreateStruct, ReadLocked);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add readlocked value failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgAddEndName(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add readlocked end name failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = TcgAddStartName(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add writelocked start name failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgAddUINT8(&CreateStruct, 0x08); /* "WriteLocked" */
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add writelocked selector failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgAddBOOLEAN(&CreateStruct, WriteLocked);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add writelocked value failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgAddEndName(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add writelocked end name failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = TcgAddEndList(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add values list end failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgAddEndName(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: add values end name failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgEndParameters(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: end parameters failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgEndMethodCall(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: end method call failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgEndSubPacket(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: end subpacket failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgEndPacket(&CreateStruct);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: end packet failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}
	Ret = TcgEndComPacket(&CreateStruct, &Size);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR, "OPAL: end com packet failed ret=%d (%s)\n",
		       Ret, tcg_result_string(Ret));
		goto out;
	}

	Ret = opal_perform_method(Session, Size, Buf, sizeof(Buf), &ParseStruct, MethodStatus,
				  0);
	if (Ret != TcgResultSuccess) {
		printk(BIOS_ERR,
		       "OPAL: set global range method failed ret=%d (%s) status=0x%02x (%s)\n",
		       Ret, tcg_result_string(Ret), *MethodStatus,
		       TcgMethodStatusString(*MethodStatus));
		goto out;
	}
	if (*MethodStatus != TCG_METHOD_STATUS_CODE_SUCCESS) {
		printk(BIOS_ERR, "OPAL: set global range rejected status=0x%02x (%s)\n",
		       *MethodStatus, TcgMethodStatusString(*MethodStatus));
		Ret = TcgResultSuccess;
		goto out;
	}
	printk(BIOS_DEBUG, "OPAL: set global range succeeded\n");

out:
	opal_explicit_bzero(Buf, sizeof(Buf));
	return Ret;
}

static TCG_RESULT opal_util_update_global_locking_range(OPAL_SESSION *Session,
							const VOID *Password,
							UINT32 PasswordLength,
							BOOLEAN ReadLocked, BOOLEAN WriteLocked)
{
	UINT8 MethodStatus = 0;
	TCG_RESULT Ret;

	NULL_CHECK(Session);
	NULL_CHECK(Password);
	printk(BIOS_DEBUG,
	       "OPAL: unlock policy begin comid=0x%04x password_len=%u read_locked=%u write_locked=%u\n",
	       Session->OpalBaseComId, PasswordLength, ReadLocked, WriteLocked);

	/* Try admin1 authority. */
	Ret = opal_start_session(Session, OPAL_UID_LOCKING_SP, TRUE, PasswordLength, Password,
				 OPAL_LOCKING_SP_ADMIN1_AUTHORITY, &MethodStatus);
	printk(BIOS_DEBUG,
	       "OPAL: admin1 start session ret=%d (%s) status=0x%02x (%s)\n",
	       Ret, tcg_result_string(Ret), MethodStatus,
	       TcgMethodStatusString(MethodStatus));
	if ((Ret == TcgResultSuccess) && (MethodStatus == TCG_METHOD_STATUS_CODE_SUCCESS)) {
		Ret = opal_update_global_locking_range(Session, ReadLocked, WriteLocked,
						       &MethodStatus);
		printk(BIOS_DEBUG,
		       "OPAL: admin1 set range ret=%d (%s) status=0x%02x (%s)\n",
		       Ret, tcg_result_string(Ret), MethodStatus,
		       TcgMethodStatusString(MethodStatus));
		if (opal_end_session(Session) != TcgResultSuccess)
			printk(BIOS_ERR, "OPAL: admin1 end session failed\n");
		if ((Ret == TcgResultSuccess) &&
		    (MethodStatus == TCG_METHOD_STATUS_CODE_SUCCESS)) {
			printk(BIOS_DEBUG, "OPAL: unlock succeeded with Admin1\n");
			return TcgResultSuccess;
		}
	}

	/* Try user1 authority. */
	Ret = opal_start_session(Session, OPAL_UID_LOCKING_SP, TRUE, PasswordLength, Password,
				 OPAL_LOCKING_SP_USER1_AUTHORITY, &MethodStatus);
	printk(BIOS_DEBUG,
	       "OPAL: user1 start session ret=%d (%s) status=0x%02x (%s)\n",
	       Ret, tcg_result_string(Ret), MethodStatus,
	       TcgMethodStatusString(MethodStatus));
	if (Ret != TcgResultSuccess)
		goto done;
	if (MethodStatus != TCG_METHOD_STATUS_CODE_SUCCESS)
		goto done;

	Ret = opal_update_global_locking_range(Session, ReadLocked, WriteLocked, &MethodStatus);
	printk(BIOS_DEBUG,
	       "OPAL: user1 set range ret=%d (%s) status=0x%02x (%s)\n",
	       Ret, tcg_result_string(Ret), MethodStatus,
	       TcgMethodStatusString(MethodStatus));
	if (opal_end_session(Session) != TcgResultSuccess)
		printk(BIOS_ERR, "OPAL: user1 end session failed\n");

done:
	if ((Ret == TcgResultSuccess) && (MethodStatus != TCG_METHOD_STATUS_CODE_SUCCESS)) {
		if (MethodStatus == TCG_METHOD_STATUS_CODE_AUTHORITY_LOCKED_OUT)
			Ret = TcgResultFailureInvalidType;
		else
			Ret = TcgResultFailure;
	}
	printk(BIOS_DEBUG,
	       "OPAL: unlock policy end ret=%d (%s) status=0x%02x (%s)\n",
	       Ret, tcg_result_string(Ret), MethodStatus,
	       TcgMethodStatusString(MethodStatus));

	return Ret;
}

u32 opal_nvme_opal_unlock(pci_devfn_t dev, u16 base_comid, const u8 *password, u8 password_len,
			  void *scratch, size_t scratch_size)
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

	if (!scratch || scratch_size < OPAL_NVME_SCRATCH_MIN_BYTES) {
		printk(BIOS_ERR, "OPAL: invalid scratch buffer\n");
		return 2;
	}

	if (smm_points_to_smram(scratch, scratch_size)) {
		printk(BIOS_ERR, "OPAL: scratch overlaps SMRAM\n");
		return 2;
	}

	queue_base = (u8 *)ALIGN_UP((uintptr_t)scratch, OPAL_NVME_PAGE_SIZE);
	if ((uintptr_t)queue_base + OPAL_NVME_SCRATCH_MIN_BYTES >
	    (uintptr_t)scratch + scratch_size) {
		printk(BIOS_ERR, "OPAL: scratch alignment overflow\n");
		rc = 2;
		goto out_early;
	}

	/* Use a 4KiB IO buffer after the queue pages. */
	io_buf = queue_base + OPAL_NVME_QUEUE_BYTES;
	printk(BIOS_DEBUG,
	       "OPAL: unlock begin dev=0x%x slot=%u func=%u comid=0x%04x scratch=0x%zx password_len=%u\n",
	       dev, PCI_SLOT(dev >> 12), PCI_FUNC(dev >> 12), base_comid, scratch_size,
	       password_len);

	if (opal_nvme_init(&nvme, dev, scratch, scratch_size)) {
		/* Fail closed and ensure stale DMA-visible buffers are cleared. */
		printk(BIOS_ERR, "OPAL: nvme init failed dev=0x%x comid=0x%04x\n", dev,
		       base_comid);
		opal_explicit_bzero(queue_base, OPAL_NVME_SCRATCH_MIN_BYTES);
		return 1;
	}
	printk(BIOS_DEBUG, "OPAL: nvme init ok dev=0x%x comid=0x%04x\n", dev, base_comid);

	memset(&Session, 0, sizeof(Session));
	Session.Nvme = &nvme;
	Session.OpalBaseComId = base_comid;
	Session.ComIdExtension = 0;

	/* Best-effort unlock. */
	Ret = opal_util_update_global_locking_range(&Session, password, password_len, FALSE,
						    FALSE);

	opal_explicit_bzero(io_buf, OPAL_NVME_PAGE_SIZE);

	/* Clear DMA-visible admin queues as well. */
	opal_explicit_bzero(queue_base, OPAL_NVME_QUEUE_BYTES);
	opal_nvme_deinit(&nvme);

	rc = (Ret == TcgResultSuccess) ? 0 : 3;
	printk(BIOS_DEBUG, "OPAL: unlock end ret=%d (%s) rc=%u\n", Ret,
	       tcg_result_string(Ret), rc);
	return rc;
out_early: {
	const uintptr_t end = (uintptr_t)scratch + scratch_size;
	const uintptr_t start = (uintptr_t)queue_base;
	if (end > start) {
		size_t safe = end - start;
		if (safe > OPAL_NVME_SCRATCH_MIN_BYTES)
			safe = OPAL_NVME_SCRATCH_MIN_BYTES;
		opal_explicit_bzero(queue_base, safe);
	}
}
	return rc;
}
