# Authenticated-variable service boundary

## Scope and oracle

This document specifies a future, small authenticated-variable service compiled
into coreboot-controlled SMM. CDK2 is only a UEFI runtime and VariablePolicy
proxy. It never loads executable code into SMRAM and never owns an authoritative
variable cache, FVB, FTW, flash transport, or authentication policy.

The compatibility oracle is StarLabs EDK2 branch `26.09` at
`aab7b589fc59b7e2b8fb7eb79519bf1a5e5a5272`. The semantic cases and source
blob identities are pinned in
`tests/lib/payload_mm_authvar_edk2_2609_semantics.tsv`. EDK2 is an oracle, not a
source import: its PI MM dispatcher, driver model, PCD, INF, FV, FVB and FTW
composition are outside this design.

The existing revision-1 `PAYLOAD_MM_AUTHVAR_CONTRACT` remains intact. Capsule
code uses it as a private protected-message staging prerequisite. Its private
SMRAM and media facts are not a public endpoint. The authenticated-variable
service uses a distinct table record and wire ABI so this work cannot silently
change the capsule contract or merge the abandoned CDK2 `b419825f2b` mirror.

This slice defines and tests ABI validators only. It has no producer, table
publication, SMI route, service implementation, storage backend, writer,
runtime caller, Kconfig selection, or security claim.

## Ownership and threats

The future authority is GPL-2 coreboot code resident in coreboot's SMM image.
Its trusted initialization must derive the exact SMMSTORE FMAP range, erase
geometry, communication reservation and platform protection state itself. The
public record contains none of those private facts except the fixed normal-RAM
mailbox address and size required by its caller.

Inputs are hostile. This includes the coreboot table after handoff, CDK2, EFI
applications, the OS, every runtime caller, PCI bus masters, stale cache lines,
another CPU entering SMM, and a reset at any media operation. Generation and
request ID detect stale or mismatched replies; they do not authenticate a
caller. Authenticated-variable signatures and the policy hierarchy authorize
changes.

The future dispatcher must copy the complete fixed-size communication frame
exactly once into a disjoint fixed SMRAM workspace. It validates and processes
only that snapshot, uses a non-waiting reentry/rendezvous gate, never follows a
caller pointer, and never rereads shared input. It clears response storage,
writes bounded response data first, then the echoed header and status, and
writes `completion` last. Every exit scrubs private request material.

The fixed communication reservation is created by coreboot and protected by
the pre-EBS default-deny DMA policy. CDK2 keeps its physical address as the SMI
authority while converting only its private runtime alias during
SetVirtualAddressMap. The SMM side always addresses the sealed physical range.
Correctness never depends on shared RAM remaining stable: after OS IOMMU
takeover the same one-copy rule remains mandatory.

The only ordinary public operations are `GET`, `NEXT`, `SET`, and `QUERY`.
`READY_TO_BOOT` and `ENTER_RUNTIME` are restriction-only lifecycle messages.
They are irreversible and idempotent. `READY_TO_BOOT` seals policy before any
external EFI image. `ENTER_RUNTIME` also performs that seal if needed, drops
boot-only visibility and can never reopen it. On S3 the reloaded SMM handler
restores the retained service as already sealed and at runtime before accepting
its first request. LegacyBoot, when supported, performs the same final seal.

There is deliberately no public `POLICY_ADD`. coreboot owns minimum policy that
cannot be relaxed. If boot policy bootstrap is later necessary, it is a
separate one-attempt, bounded, DMA-gated channel closed before external code;
it is not an operation in this runtime service.

## Wire contract

`LB_TAG_AUTHVAR_SERVICE_ENDPOINT` describes one 64-byte endpoint. Required
flags assert coreboot SMM ownership, fixed communication, pre-EBS DMA
protection, CPU rendezvous, SMM policy ownership, absence of raw SMMSTORE,
mandatory SMM BIOS write protection, and sealed lifecycle support. The record
publishes only generation, mailbox geometry, bounded name/data capacities and
an eight-bit APM trigger. It exposes no SMRAM, SPI, store, block, erase, or raw
flash address or operation.

The fixed 128-byte message header is followed by exactly
`maximum_name_size` bytes for UTF-16 names and then, on an eight-byte boundary,
exactly `maximum_data_size` bytes. The endpoint message size must equal that
complete layout. Inline fields contain no address or offset. Requests initialize
status and completion to their pending sentinels. Responses echo immutable
request identity, publish operation-specific bounded sizes, and change
completion only after every other response byte.

Delete is `SET` with zero attributes and zero data. Append uses the standard
append attribute and still passes authentication and timestamp policy. The
counter-based authenticated-write attribute is structurally rejected; the
target supports `EFI_VARIABLE_AUTHENTICATION_2` time-based writes. Structural
admission does not imply semantic acceptance by a future authority.

## Required EDK2-compatible semantics

The final implementation preserves the existing EDK2 authenticated variable
store, firmware-volume header, working block and spare block geometry. It does
not invent an incompatible journal. EDK2-to-CDK2 and CDK2-to-EDK2 boots must
read the same committed store, including interrupted FTW recovery.

The oracle covers ordinary and time-authenticated create, replace, append,
empty payload and delete behavior; exact attribute transitions; PK, KEK, db,
dbx, dbt, certdb and certdbv; SetupMode, SecureBoot, AuditMode, DeployedMode,
VendorKeys and CustomMode; SHA-256, SHA-384 and SHA-512 PKCS7; timestamp replay;
UTF-16 and GetNextVariableName order; quota and QueryVariableInfo; runtime
visibility; EndOfDxe/ReadyToBoot, ExitBootServices, SetVirtualAddressMap, S3
and LegacyBoot where supported.

Physical-presence and CustomMode bypasses are not granted by a payload boolean.
Until coreboot has a trustworthy presence source, the native authority remains
more restrictive and records that deliberate divergence. SetupMode enrollment
and signed updates remain separate semantics.

## Future atomic composition

Production selection is one fail-closed composition, not incremental exposure.
It must simultaneously install the typed authority and close every alternate
writer: coreboot CFR/`efi_fv_set_option` mutations are routed through the same
internal authority; raw SMMSTORE read/write/clear and its table are absent;
full-flash SMMSTORE is absent; CDK2 FVB, FTW and local variable authority are
absent; SPI console, internal flashrom and alternate capsule writers cannot
touch the variable range; capsule writes preserve it. The platform forces
non-runtime `BOOTMEDIA_SMM_BWP`, reads the hardware lock back, and scopes any
temporary write enable to an admitted SMM erase/program operation. Reads never
open write protection.

Before that composition, host tests must cover cross-repository layout parity,
hostile mutation/fuzz cases, O0, O2, strict warnings, ASan and UBSan, and prove
that no firmware Makefile calls the validator. Later QEMU tests add real SMI,
reentry, DMA denial, signed-variable, power-cut, reboot, S3 and EFI runtime
evidence. QEMU cannot prove Intel EISS/SMM_BWP or real SPI power-loss behavior;
those remain hardware gates.
