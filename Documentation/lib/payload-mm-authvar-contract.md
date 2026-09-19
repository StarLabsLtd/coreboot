# Payload-MM authenticated-variable boundary

`PAYLOAD_MM_AUTHVAR_CONTRACT` is a default-off prerequisite for a native CDK2
authenticated-variable service running inside a coreboot-owned SMM environment.
It is deliberately smaller than the Payload-MM loader and MM dispatcher proposed
in coreboot Gerrit changes 89031 and 89151.

The contract accepts platform facts only when coreboot owns SMM entry, SPI writes
are SMM-only, the complete variable-store region is owned by that SMM path, the
communication buffer is bounded and outside SMRAM, and no raw/full-flash
transport remains available. The store must be a proper subset of boot media,
use power-of-two geometry and contain at least three erase blocks.

The ramstage builder validates the proposed contract. The separate SMM runtime
installs one private copy in its own static storage, exactly once. A mandatory
platform hook proves that the complete static authority object is protected
SMM storage; outputs that alias that object are rejected. Runtime code
first proves that the complete request descriptor is in the authoritative
communication range, snapshots it once, validates that snapshot, then copies
the opaque message into caller-provided SMRAM storage. Native CDK2 must parse
only those trusted copies and never reread shared RAM.

The single installation attempt is consumed before inspecting its input. A
malformed initialization therefore fails closed and cannot reopen authority
installation through a later SMI.

The request contains no flash address, block number, erase command or write
command. Authenticated-variable parsing, signature and timestamp checks, replay
protection, append/delete policy, durable transaction state and final mutation
authorization belong to native CDK2 code. The contract does not claim that any
of those policies exist.

## Dormant SystemFmp state policy

The SMM-only SystemFmp state layer narrows the opaque message to one fixed
64-byte ABI. The layout was checked independently rather than inferred from its
fields: the transaction is at byte 16, the 20-byte data is at byte 40, the last
reserved word is at byte 60, and the structure and carrying address require
eight-byte alignment. The only operations are a fixed-size read, a 20-byte `FmpState`
write, removal of one legacy state variable, and irreversible closure of this
state channel.

Coreboot trusted initialization must install the namespace `guid_t` in common
UEFI in-memory byte order, hardware
instance and trusted lowest supported version once. The complete policy is
copied into protected SMM storage only after the parent authority is installed.
The shared message carries no identity. SMM maps its typed key to `FmpState`,
`FmpVersion`, `FmpLsv`, `LastAttemptStatus` or `LastAttemptVersion`. A nonzero
hardware instance adds exactly 16 uppercase, zero-padded hexadecimal digits;
zero adds no suffix.

The state parser accepts only canonical zero-or-one validity bytes, exact NV and
boot-service attributes, exact data sizes, nondecreasing validity and lowest
supported version. The sealed trusted floor is returned to the future policy
owner; it is not forced into an EDK-compatible checkpoint or migration write.
Only the four legacy keys can be removed, and only when the caller supplies a
protected, valid combined state snapshot. A successful close rejects every
later state request, including another close. Transactions must increase, but
their value is replay hardening rather than caller authentication.

Both the already-copied message and optional current state must be bounded SMM
buffers. The parser snapshots each once, rejects aliases with its protected
output or either authority, and never rereads the input. It emits a typed
SMM-owned command containing the sealed identity and constructed name. It does
not execute that command.

This layer deliberately provides no SMI number, producer, dispatcher, generic
`SetVariable`, SMMSTORE command, variable engine, flash backend or persistence
claim. Its write result cannot satisfy the CDK2 durable-state callback until a
future resident variable owner proves atomic, reset-safe, rollback-protected
commit and readback. The platform must also source the identity from trusted
live firmware facts; a payload-provided GUID or hardware instance is not
authority.

| State gate | Status |
| --- | --- |
| Sealed identity and semantic message validation | Implemented, unselected |
| Trusted platform identity producer | Open |
| SMI transport and Payload-MM dispatcher | Open |
| Atomic rollback-protected variable owner | Open |
| Mandatory pre-handoff and S3 lifecycle closure | Open |

## Source comparison

The local Gerrit-derived Payload-MM series demonstrates SMRAM reservation,
one-attempt loader hardening, Intel SMI entry and SPI geometry, but also imports
a broad loader/dispatcher lifecycle. EDK2 26.09 `BlSmmCpuPayloadMm` similarly
provides an MM CPU environment and rejects forbidden communication addresses.
Neither is copied here: coreboot retains its silicon SMM entry and this ABI only
admits a previously reserved communication range and variable-store geometry.

## Production gates

No producer, coreboot-table record, SMI command or dispatcher is installed. The
runtime authority is compiled only into the SMM module; ramstage state is never
treated as runtime authority.
`Q35_PAYLOAD_MM_AUTHVAR_TEST_PROOF` only compiles this library for the QEMU Q35
proof build; it does not weaken these production gates.
Before selection, a platform must provide all of the following through static,
platform-owned verification hooks, never caller-supplied trust booleans:

* an SMRAM reservation and coreboot-owned SMI entry that survive S3;
* a reserved runtime communication range checked against every SMRAM and
  protected-memory range before copying a request snapshot;
* an SMM-only SPI backend whose descriptor/FMAP policy proves exact ownership
  of the variable-store region and keeps BIOS/full-flash access unreachable;
* a matched native CDK2 handler that validates authenticated-variable policy and
  closes registration before OS handoff;
* a trusted SystemFmp identity producer and mandatory state-channel close before
  any external EFI image, OS handoff or S3 resume;
* a resident variable engine whose successful state write means reset-safe,
  rollback-protected nonvolatile commit and verified readback;
* hostile QEMU evidence followed by Intel and AMD hardware validation.

The generic SMMSTORE raw read/write/clear interface and its capsule full-flash
modifier are not a backend for this contract. Enabling this library alone does
not provide authenticated variables.

Host evidence is provided by:

```
tests/lib/payload_mm_authvar_test.sh
```
