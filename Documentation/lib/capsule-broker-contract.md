# Dormant capsule broker contract

The capsule broker is an unselected, one-shot SMM contract between a trusted
early CDK2 coordinator and coreboot's bounded capsule writer. It is not a raw
flash service, a generic SMMSTORE extension or an OS runtime interface.

## Ownership and ABI

Trusted coreboot initialization owns the immutable writable-region policy,
boot-media and erase geometry, SMMSTORE exclusion, media backend, scratch
storage, generation, communication message and full-image staging span. The
complete policy is copied into protected SMM storage exactly once. Installation
requires platform proofs for reserved communication and staging memory, DMA
protection, SMM-only SPI ownership, absence of raw flash access and active CPU
rendezvous.

Callback pointers and their media, digest, authentication and proof contexts
are part of that
snapshot. Each non-null context has an explicit size capped at 128 bytes and is
copied into the protected authority before installation succeeds. Callbacks
receive only those copies. Context shape is checked before copying, then the
policy snapshot is repointed to the protected copies before any context-
dependent proof callback runs. The exact validated snapshot is committed;
caller-owned context bytes are never reread afterward. Contexts must be
self-contained authority: an
embedded pointer may address media data, the fixed message or hardware, but
must not redirect policy or proof decisions to mutable payload memory.

The same rules apply to the authentication callback and its bounded context.
The callback's success contract is complete authentication and policy approval
of the exact fixed staging image, including signature, capsule format,
dependency and board binding. This tree defines and seals that port but
supplies no provider or trust anchors.

`LB_TAG_CAPSULE_BROKER_ENDPOINT` is an 80-byte public description of the
already-installed endpoint. It carries one nonzero cold-boot generation, one
fixed 96-byte communication message, one fixed full-image staging span and a
typed byte-wide APM trigger. It carries no flash offset, writable route or raw
media command. The table record is not authority and this commit provides no
producer for it.

The fixed, eight-byte-aligned message is 96 bytes. `APPLY` supplies only the
generation, increasing transaction, exact image size, attempted version and a
SHA-256 digest of the fixed staging span. `CLOSE` requires every image and
digest field to be zero. There are no caller addresses or region arrays.

## Checkpoint and immutable image

CDK2 must copy the raw image into the fixed staging span before authentication.
Authentication, MSS1 parsing, dependency checking, board binding, hashing and
the broker call must all use that exact span. The span and communication
message remain DMA-inaccessible from before the copy through writer completion.
SMM also requires CPU rendezvous while hashing and writing.

The broker snapshots the shared message once and never rereads request fields.
It recomputes SHA-256 in SMM, constructs the writer plan exclusively from its
sealed staging address and sealed routes, and calls the existing bounded
writer. Media callbacks recheck the DMA, rendezvous, SMM-SPI and no-raw-flash
proofs before every operation.

Before an executor may treat a staged CHECK or SET as valid, the broker hashes
its fixed staging span and matches the staged digest, invokes the sealed
authentication callback with a protected local copy of its context, then
rechecks the DMA/rendezvous guard and hashes the complete span again. The
callback receives no sealed-context address and must not retain its synchronous
image or local-context arguments. Staging mutation fails the second digest
check. Only SET retains an exact transaction, attempted
version and digest authorization. CHECK is validation-only and cannot enable a
checkpoint grant. The checkpoint consumes the SET authorization and the APPLY
message must match its digest, so no unauthenticated or substituted image can
reach media through the internal grant path.

Authentication is protected by a fail-closed in-progress latch established
before the first proof or hash callback. Nested authentication, checkpoint
grant and broker handling are rejected while it is set. Broker state is
revalidated after every callback boundary. A provider-triggered S3 close is
irreversible and causes the outer authentication to fail; the latch is cleared
on every ordinary success and failure return.

An `APPLY` additionally requires a one-use SMM-internal checkpoint grant for
the same generation, transaction and attempted version. Only the protected
variable owner may create that grant, and only after the combined FMP state has
been atomically committed with unsuccessful attempt status. The companion
internal checkpoint engine reads and commits through that same protected owner,
then performs a separate exact-state readback. It remains dormant because this
series supplies no reset-safe, rollback-protected variable backend.

## Lifecycle

A malformed installation consumes the sole installation attempt. A matching-
generation malformed request poisons the channel. A valid `APPLY` consumes its
grant and closes the broker before hashing or media access, so digest, erase,
write, read or verification failure cannot be retried in the same boot. A
valid `CLOSE` is also irreversible. A stale generation is rejected without
opening new authority. S3 forces the channel closed and cannot reinstall or
reuse its generation.

The production phase owner must close the broker before loading any external
EFI image, handing control to an OS or resuming from S3. The digest proves that
the bytes presented to SMM equal the coordinator's snapshot; it is not itself
authentication authority.

## Writer and status

The broker calls `capsule_apply_policy_verified()`. That writer completes all
plan and backend validation before its first media operation, accepts only the
immutable route policy, excludes SMMSTORE, touches only erase-aligned listed
regions and compares readback after each block. Every unlisted byte is
preserved.

The writer currently returns only success or failure. The broker therefore
returns FMP success or generic unsuccessful status and does not invent device-
specific status values. A partial-media failure remains a reset/recovery case.

## Gate

| Broker gate | Status |
| --- | --- |
| Endpoint/message ABI and sealed state machine | Implemented, unselected |
| Hostile O0/O2/ASan/UBSan model | Implemented |
| Trusted endpoint/policy producer | Open |
| Protected authentication-provider port | Implemented, unselected |
| Authentication provider and trust anchors | Open |
| Typed checkpoint engine and grant ordering | Implemented, unselected |
| Atomic rollback-protected variable backend | Open |
| DMA-protected staging and SMM rendezvous | Open |
| SMI dispatcher and endpoint publication | Open |
| Production SPI backend | Open |
| CDK2 composition and close-before-external-code proof | Open |
| QEMU power-loss and Intel/AMD hardware evidence | Open |

`Q35_CAPSULE_BROKER_TEST_PROOF` only compiles the SMM object. It publishes no
record, registers no SMI command and supplies no checkpoint, hashing or flash
provider. Host evidence is provided by:

```
tests/lib/capsule_broker_test.sh
tests/lib/payload_mm_fmp_checkpoint_test.sh
```
