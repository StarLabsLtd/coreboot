/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Fixed-policy semantics adapted from Star Labs DAVE EnrollDefaultKeys,
 * 5e4c1fb3b40719fdd62ae25a534c333c765ad649, Copyright 2026 Star Labs Systems. */
#include <Uefi.h>
#include <Guid/GlobalVariable.h>
#include <Guid/ImageAuthentication.h>
#include <Guid/AuthenticatedVariableFormat.h>
#include <Guid/SmmVariableCommon.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/AuthVariableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MmServicesTableLib.h>
#include <Library/VarCheckLib.h>
#include <Library/VariablePolicyLib.h>
#include <Library/VariablePolicyHelperLib.h>
#include <Protocol/SmmVariable.h>
#include "database.h"
#include FIXED_POLICY_ASSETS

#define VARIABLE_CAPACITY 0x10000
#define KEY_ATTRIBUTES (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | \
	EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS)
#define DBX_ATTRIBUTES (KEY_ATTRIBUTES | EFI_VARIABLE_RUNTIME_ACCESS)
#define CONTROL_ATTRIBUTES (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS)
#define STATE_ATTRIBUTES (EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

/* One query, no caller-supplied keys: phase 0 prepares, phase 1 verifies after
 * EndOfDxe. Both fields are IA32 values in the ASS mailbox. */
struct fixed_query {
	UINT32 phase;
	UINT32 status;
};

static EFI_GUID fixed_guid = { 0xcc4b3ec3, 0x6cce, 0x4a6a,
	{ 0xa6, 0x73, 0xc4, 0xb4, 0x4f, 0x74, 0x45, 0x61 } };
static EFI_SMM_VARIABLE_PROTOCOL *variables;
static EFI_STATUS initialization = EFI_NOT_READY;
static BOOLEAN checks_active, restart_required;
static UINT8 *scratch;
static EFI_HANDLE query_handle;
static VOID *variable_registration;
extern AUTH_VAR_LIB_CONTEXT_IN mAuthContextIn;
static AUTH_VAR_LIB_FIND_VARIABLE find_auth_variable;

struct fixed_key {
	CHAR16 *name;
	EFI_GUID *guid;
	const UINT8 *data;
	UINTN size;
	UINT32 attributes;
};

static const UINT8 enabled = 1, standard = 0;
static const struct fixed_key keys[] = {
	{ L"PK", &gEfiGlobalVariableGuid, fixed_pk, sizeof(fixed_pk), KEY_ATTRIBUTES },
	{ L"KEK", &gEfiGlobalVariableGuid, fixed_kek, sizeof(fixed_kek), KEY_ATTRIBUTES },
	{ L"db", &gEfiImageSecurityDatabaseGuid, fixed_db, sizeof(fixed_db), KEY_ATTRIBUTES },
	{ L"SecureBootEnable", &gEfiSecureBootEnableDisableGuid, &enabled, 1, CONTROL_ATTRIBUTES },
	{ L"CustomMode", &gEfiCustomModeEnableGuid, &standard, 1, CONTROL_ATTRIBUTES },
};

static EFI_STATUS EFIAPI find_fixed_auth_variable(CHAR16 *name, EFI_GUID *guid,
	AUTH_VARIABLE_INFO *info)
{
	EFI_STATUS status = find_auth_variable(name, guid, info);

	if (status != EFI_NOT_FOUND || !checks_active || !mAuthContextIn.AtRuntime() ||
	    !CompareGuid(guid, &gEfiGlobalVariableGuid))
		return status;
	/* Public GetVariable still hides BS-only keys. Authentication may use the
	 * immutable keys checked against the store before this callback is installed. */
	for (UINTN i = 0; i < ARRAY_SIZE(keys); i++) {
		if (!CompareGuid(guid, keys[i].guid) || StrCmp(name, keys[i].name))
			continue;
		info->Data = (VOID *)keys[i].data;
		info->DataSize = keys[i].size;
		info->Attributes = keys[i].attributes;
		return EFI_SUCCESS;
	}
	return status;
}

static EFI_STATUS read_variable(CHAR16 *name, EFI_GUID *guid, UINT32 attributes,
	UINTN *size)
{
	UINT32 actual = 0;
	EFI_STATUS status;

	*size = VARIABLE_CAPACITY;
	status = variables->SmmGetVariable(name, guid, &actual, size, scratch);
	if (EFI_ERROR(status))
		return status;
	return actual == attributes ? EFI_SUCCESS : EFI_SECURITY_VIOLATION;
}

static EFI_STATUS validate_keys(void)
{
	UINTN size;
	EFI_STATUS status;

	for (UINTN i = 0; i < ARRAY_SIZE(keys); i++) {
		status = read_variable(keys[i].name, keys[i].guid, keys[i].attributes, &size);
		if (EFI_ERROR(status))
			return status;
		if (size != keys[i].size || CompareMem(scratch, keys[i].data, size))
			return EFI_SECURITY_VIOLATION;
	}
	status = read_variable(L"SecureBoot", &gEfiGlobalVariableGuid, STATE_ATTRIBUTES, &size);
	if (EFI_ERROR(status))
		return status;
	if (size != 1 || scratch[0] != 1)
		return EFI_SECURITY_VIOLATION;
	status = read_variable(L"SetupMode", &gEfiGlobalVariableGuid, STATE_ATTRIBUTES, &size);
	if (EFI_ERROR(status))
		return status;
	return size == 1 && scratch[0] == 0 ? EFI_SUCCESS : EFI_SECURITY_VIOLATION;
}

static EFI_STATUS validate_dbx(const UINT8 *required, UINTN required_size)
{
	UINTN size;
	EFI_STATUS status = read_variable(L"dbx", &gEfiImageSecurityDatabaseGuid,
		DBX_ATTRIBUTES, &size);

	if (EFI_ERROR(status))
		return status;
	return StaticSecureBootDatabaseContains(required, required_size, scratch, size);
}

static EFI_STATUS prepare_policy(void)
{
	EFI_STATUS status = validate_keys();

	if (EFI_ERROR(status))
		return status;
	status = validate_dbx(fixed_dbx, sizeof(fixed_dbx));
	if (status != EFI_SECURITY_VIOLATION || checks_active)
		return status;
	/* Only an exact static predecessor can receive the authenticated append.
	 * Never replace user keys or enter CustomMode to make migration succeed. */
	status = validate_dbx(fixed_dbx_baseline, sizeof(fixed_dbx_baseline));
	if (EFI_ERROR(status))
		return status;
	/* Flash may commit before a later write fails. Never admit this boot after
	 * attempting maintenance, even when the owner reports an error. */
	restart_required = TRUE;
	status = variables->SmmSetVariable(L"dbx", &gEfiImageSecurityDatabaseGuid,
		DBX_ATTRIBUTES | EFI_VARIABLE_APPEND_WRITE, sizeof(fixed_dbx_update),
		(VOID *)fixed_dbx_update);
	if (EFI_ERROR(status))
		return status;
	status = validate_dbx(fixed_dbx, sizeof(fixed_dbx));
	return EFI_ERROR(status) ? status : EFI_WARN_RESET_REQUIRED;
}

static EFI_STATUS EFIAPI check_update(CHAR16 *name, EFI_GUID *guid,
	UINT32 attributes, UINTN size, VOID *data)
{
	for (UINTN i = 0; i < ARRAY_SIZE(keys); i++)
		if (CompareGuid(guid, keys[i].guid) && !StrCmp(name, keys[i].name))
			return EFI_WRITE_PROTECTED;
	if (!CompareGuid(guid, &gEfiImageSecurityDatabaseGuid) || StrCmp(name, L"dbx"))
		return EFI_SUCCESS;
	if ((attributes & ~EFI_VARIABLE_APPEND_WRITE) != DBX_ATTRIBUTES || !size)
		return EFI_SECURITY_VIOLATION;
	/* VarCheck receives the owner-copied payload, without the AUTH2 envelope.
	 * Authentication still runs in the variable service after this predicate. */
	if (attributes & EFI_VARIABLE_APPEND_WRITE)
		return validate_dbx(fixed_dbx, sizeof(fixed_dbx));
	return StaticSecureBootDatabaseContains(fixed_dbx, sizeof(fixed_dbx), data, size);
}

static EFI_STATUS EFIAPI query_policy(EFI_HANDLE handle, CONST VOID *context,
	VOID *buffer, UINTN *size)
{
	struct fixed_query query;
	EFI_STATUS status;
	VOID *write_ready;

	(void)handle;
	(void)context;
	/* StandaloneMmCore validates and copies the primary message into SMRAM. */
	if (!buffer || !size || *size != sizeof(query))
		return EFI_INVALID_PARAMETER;
	CopyMem(&query, buffer, sizeof(query));
	status = initialization;
	if (!EFI_ERROR(status)) {
		if (query.phase > 1)
			status = EFI_INVALID_PARAMETER;
		else if (query.phase == 1 && !checks_active)
			status = EFI_NOT_READY;
		else if (EFI_ERROR(gMmst->MmLocateProtocol(&gSmmVariableWriteGuid,
			NULL, &write_ready)))
			status = EFI_NOT_READY;
		else if (!IsVariablePolicyEnabled())
			status = EFI_SECURITY_VIOLATION;
		else if (restart_required)
			status = EFI_WARN_RESET_REQUIRED;
		else
			status = prepare_policy();
		if (status == EFI_SUCCESS && !checks_active) {
			/* Activate the owner checks after authenticated-variable write
			 * initialization. Do not signal MM EndOfDxe here: that also locks
			 * policy registration needed by later payload consumers. */
			VarCheckLibInitializeAtEndOfDxe(NULL);
			checks_active = TRUE;
			status = prepare_policy();
		}
		if (status == EFI_SUCCESS && !find_auth_variable) {
			find_auth_variable = mAuthContextIn.FindVariable;
			mAuthContextIn.FindVariable = find_fixed_auth_variable;
		}
	}
	query.status = (UINT32)status;
	CopyMem(buffer, &query, sizeof(query));
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI variable_ready(CONST EFI_GUID *guid, VOID *interface,
	EFI_HANDLE handle)
{
	(void)guid;
	(void)handle;
	if (variables)
		return initialization;
	variables = interface;
	if (!variables)
		return EFI_INVALID_PARAMETER;
	for (UINTN i = 0; i < ARRAY_SIZE(keys); i++) {
		VARIABLE_POLICY_ENTRY *policy = NULL;
		EFI_STATUS status = CreateBasicVariablePolicy(keys[i].guid, keys[i].name,
			keys[i].size, keys[i].size, keys[i].attributes, ~keys[i].attributes,
			VARIABLE_POLICY_TYPE_LOCK_NOW, &policy);
		if (!EFI_ERROR(status)) {
			status = RegisterVariablePolicy(policy);
			FreePool(policy);
		}
		if (EFI_ERROR(status)) {
			initialization = status;
			return status;
		}
	}
	initialization = EFI_SUCCESS;
	return EFI_SUCCESS;
}

EFI_STATUS EFIAPI fixed_policy_init(VOID)
{
	EFI_STATUS status;

	scratch = AllocatePool(VARIABLE_CAPACITY);
	if (!scratch)
		return EFI_OUT_OF_RESOURCES;
	status = VarCheckLibRegisterSetVariableCheckHandler(check_update);
	if (!EFI_ERROR(status))
		status = gMmst->MmRegisterProtocolNotify(&gEfiSmmVariableProtocolGuid,
			variable_ready, &variable_registration);
	if (!EFI_ERROR(status))
		status = gMmst->MmiHandlerRegister(query_policy, &fixed_guid, &query_handle);
	return status;
}

EFI_STATUS EFIAPI fixed_policy_cleanup(VOID)
{
	EFI_STATUS status;

	if (query_handle) {
		status = gMmst->MmiHandlerUnRegister(query_handle);
		if (EFI_ERROR(status))
			return status;
		query_handle = NULL;
	}
	if (variable_registration) {
		status = gMmst->MmRegisterProtocolNotify(&gEfiSmmVariableProtocolGuid,
			NULL, &variable_registration);
		if (EFI_ERROR(status))
			return status;
		variable_registration = NULL;
	}
	if (find_auth_variable)
		mAuthContextIn.FindVariable = find_auth_variable;
	if (scratch) {
		FreePool(scratch);
		scratch = NULL;
	}
	return EFI_SUCCESS;
}
