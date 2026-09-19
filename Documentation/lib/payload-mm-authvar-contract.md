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

The internal request-staging adapter composes these two accepted parsers without
adding another wire format. Trusted initialization seals one exact, aligned
workspace in SMRAM after independently proving both the adapter authority and
the whole workspace are protected. Installation is one-attempt and fail-closed.
Each request descriptor and its exact 64-byte SystemFmp message are copied once
into that workspace before semantic parsing. Shared request or message mutation
afterward cannot change the staged command.

Only one command may be outstanding. A second prepare is rejected without
touching the snapshot, and completion requires the exact staged transaction
before the workspace is cleared. Invalid bounds, alignment, generation, size,
aliasing, replay or message semantics leave no staged command. The adapter does
not source `current_state`: a future protected variable owner must read that
snapshot from its authoritative rollback-protected store before calling the
internal API. Consequently this commit still executes no read, write, removal
or close operation and supplies no result writer.

## Protected SystemFmp variable owner port

The next internal prerequisite fixes the storage boundary without pretending
that a storage engine exists. Trusted initialization installs one owner port
after the SystemFmp identity authority. Its callbacks and at most 128 bytes of
context are copied once into protected SMM storage. The context passed to a
callback is immutable. Any pointer embedded in it must refer only to a
protected variable owner, never payload-visible state or a caller-selected
route.

The port exposes only five sealed SystemFmp keys. Coreboot constructs the exact
namespace, hardware-instance suffix and key-specific variable name; the caller
cannot provide any of them. Records are fixed, eight-byte-aligned 48-byte
objects containing a rollback-protected sequence, canonical presence bit,
exact attributes and size, and at most the canonical 20-byte combined state.
Legacy values are exactly four bytes with a zero tail. An absent record has no
attributes, size or data.

Reads accept only protected output storage and validate the complete returned
record. State commits require protected, nonaliasing current and candidate
records, sequence plus one, canonical nondecreasing validity and lowest
supported version. Legacy removal is the same operation with an exact absent
candidate. The backend `commit` is compare-and-commit against the supplied
current record. Success must mean the candidate is atomically reset-safe and
rollback-protected, while failure preserves the prior readable record. The port
then performs a fresh authoritative read and requires exact byte equality.

This tree supplies no backend satisfying that contract. The port is not a
generic variable API and cannot select a GUID, name, attributes, data size or
arbitrary deletion. The staged-command executor remains open; it must source
its current state through this owner rather than shared RAM.

This layer deliberately provides no SMI number, producer, dispatcher, generic
`SetVariable`, SMMSTORE command, flash backend or persistence claim. The
capsule-broker build adds one internal checkpoint-only engine described below;
it is not reachable through this state-message ABI. The platform must source
the identity from trusted live firmware facts; a payload-provided GUID or
hardware instance is not authority.

## Internal durable checkpoint engine

When both dormant contracts are built, the SMM module contains one internal
bridge from the SystemFmp state authority to the capsule broker's typed grant.
It has no communication operation and cannot write an arbitrary namespace,
name, attributes or value. Its only mutation preserves the authoritative
20-byte combined state, sets canonical `LastAttemptStatus` and
`LastAttemptVersion` validity, records unsuccessful status and the admitted
attempted version, and retains the existing version and lowest-supported
version fields.

Trusted initialization installs one backend snapshot. Callback pointers and a
bounded context of at most 128 bytes are copied into protected SMM storage. The
copy is complete before the protection callback can mutate caller storage, and
the caller's backend or context is never reread. An embedded pointer in that
context may identify the protected storage engine, but must not redirect policy
to payload-visible memory.

The backend API is intentionally state-specific. `read` returns the exact
combined variable together with its rollback-protected sequence. `commit`
performs a compare-and-commit from that exact current record to sequence plus
one. Success has a strict contract: the new record is atomically committed to
reset-safe rollback-protected nonvolatile media before return; power loss or
failure leaves the prior record completely readable. The engine then performs
a second authoritative read and requires exact sequence, attributes, size,
canonical data and byte equality. Callback inputs are fresh snapshots and any
attempt to modify their identity or record bytes fails closed. This commit
supplies no backend that can make
those guarantees and does not treat a generic SMMSTORE write as one.

Missing, malformed or noncanonical state, sequence wrap, a version below the
sealed or durable floor, commit failure, and failed or mismatched readback all
stop before the broker grant. An already identical failure checkpoint avoids a
write but still requires an authoritative exact read. Cold-boot generation and
strictly increasing transaction values prevent replay. Only after this proof
does the engine call the broker's typed grant with the same generation,
transaction and attempted version. Grant failure remains fail-closed after the
safe durable checkpoint and can never reach media erase. The bridge consumes
its authority before that first grant invocation. Later requests cannot replace
the durable checkpoint while an earlier broker grant remains live; only a
failure before grant invocation can be retried with a newer transaction.

| State gate | Status |
| --- | --- |
| Sealed identity and semantic message validation | Implemented, unselected |
| Protected request snapshot and typed staging | Implemented, unselected |
| Protected typed SystemFmp variable owner port | Implemented, unselected |
| Trusted platform identity producer | Open |
| SMI transport, operation executor and result writer | Open |
| Typed checkpoint engine and exact readback | Implemented, unselected |
| Atomic rollback-protected variable backend | Open |
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
* a matched native CDK2 handler that validates authenticated-variable policy,
  sources authoritative current state, executes typed commands, writes bounded
  results and closes registration before OS handoff;
* a trusted SystemFmp identity producer and mandatory state-channel close before
  any external EFI image, OS handoff or S3 resume;
* a resident variable backend whose successful state write means reset-safe,
  rollback-protected nonvolatile compare-and-commit and verified readback;
* serialized SMM dispatch for the checkpoint engine and its one-shot broker;
* hostile QEMU evidence followed by Intel and AMD hardware validation.

The generic SMMSTORE raw read/write/clear interface and its capsule full-flash
modifier are not a backend for this contract. Enabling this library alone does
not provide authenticated variables.

Host evidence is provided by:

```
tests/lib/payload_mm_authvar_test.sh
tests/lib/payload_mm_fmp_dispatch_test.sh
tests/lib/payload_mm_fmp_owner_test.sh
```
