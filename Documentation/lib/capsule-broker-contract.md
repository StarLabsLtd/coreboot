# Dormant capsule broker contract

The capsule broker is an unselected, one-shot SMM contract between a trusted
early CDK2 coordinator and coreboot's bounded capsule writer. It is not a raw
flash service, a generic SMMSTORE extension or an OS runtime interface.

## Ownership and ABI

Trusted coreboot initialization owns the immutable writable-region policy,
boot-media and erase geometry, SMMSTORE exclusion, media backend, scratch
storage, generation, reserved communication range and capsule staging
capacity. The complete policy is copied into protected SMM storage exactly
once. Installation
requires platform proofs for reserved communication and staging memory, DMA
protection, SMM-only SPI ownership, absence of raw flash access and active CPU
rendezvous.

Callback pointers and their media, digest, authentication and proof contexts
are part of that snapshot. Each non-null context has an explicit size capped
at 128 bytes and is
copied into the protected authority before installation succeeds. Callbacks
receive only those copies. Context shape is checked before copying, then the
policy snapshot is repointed to the protected copies before any context-
dependent proof callback runs. The exact validated snapshot is committed;
caller-owned context bytes are never reread afterward. Contexts must be
self-contained authority: an embedded pointer may address media data or
hardware, but
must not redirect policy or proof decisions to mutable payload memory.

The same rules apply to the authentication callback and its bounded context.
The callback's success contract is complete authentication and policy approval
of the exact staged capsule envelope, including signature, capsule format,
dependency and board binding. It returns a bounded offset and size for the raw
ROM inside that envelope, never a pointer. The unselected Payload-MM provider
implements that contract without a callback context. Its one-attempt policy
install copies
the XDR trust set, image GUID, platform version floor, ROM size and mainboard
vendor/part into protected storage; no caller pointer is retained. Installation
requires vendor/part to equal coreboot's internal mainboard configuration, so a
public table or payload build argument cannot select board authority. No
platform installs that policy or supplies trust anchors in this tree.

`LB_TAG_CAPSULE_BROKER_ENDPOINT` is an 80-byte public description of the
already-installed endpoint. It carries one nonzero cold-boot generation, one
reserved 168-byte communication range, one fixed staging capacity and a typed
byte-wide APM trigger. It carries no flash offset, writable route or raw media
command. The communication range has a fixed, pointer-free layout: a 40-byte
request at offset 0, the 88-byte capsule intent at offset 40 and a 40-byte
result at offset 128. Request and intent repeat the generation and transaction;
both pairs must match the installed broker and each other. There is no caller-
selected message address and no shared-message APPLY entry point. The table
record is not authority and this commit provides no producer for it.

The dormant transport dispatcher copies the request and intent into protected
locals, validates their fixed revisions, sizes, offsets, reserved fields and
bindings, then calls only the synchronous staged-intent executor. Increasing
transactions are consumed before execution, so failure, replay and stale
requests cannot invoke it again. A busy latch rejects reentry. The result is
bound to the copied generation, transaction and attempted version. Its status
and completion marker remain pending during execution; status is committed
after all other response bytes and the result marker is committed last. A
caller must read the result marker first after notification, then reject a
response whose revision, size, generation or transaction does not match its
request. This commit registers no SMI, publishes no endpoint and selects no
platform transport.

## Checkpoint and immutable image

CDK2 must copy the complete authenticated capsule envelope into the staging
capacity before authentication and report its exact nonzero byte count in the
typed intent. Authentication, MSS1 parsing, dependency checking, board binding,
hashing and the broker call must all use that exact envelope. The occupied span
and reserved communication range remain DMA-inaccessible through writer
completion.
SMM also requires CPU rendezvous while hashing and writing.

The broker accepts APPLY only from the exact capsule intent retained by typed
protected dispatch. It recomputes SHA-256 in SMM, constructs the writer plan
exclusively from its sealed staging address and sealed routes, and calls the
existing bounded writer. Media callbacks recheck the DMA, rendezvous, SMM-SPI
and no-raw-flash proofs before every operation.

Before an executor may treat a staged CHECK or SET as valid, the broker hashes
the complete occupied envelope and matches the staged digest, invokes the sealed
authentication callback with a protected local copy of its context, validates
the returned raw-ROM offset, exact sealed ROM size and overflow-safe
containment, then rechecks the DMA/rendezvous guard and hashes the complete
envelope again. The callback receives the complete protected owner snapshot and
must not retain its
synchronous image, owner or local-context arguments. The native provider
independently rereads the owner and requires an exact record match, including
sequence and data. Staging mutation fails the second digest check. Only SET
retains an exact transaction, attempted version, digest and raw-ROM span
authorization. CHECK is validation-only and
retains neither a span nor a checkpoint grant. The checkpoint consumes the SET
authorization and seals that same span into the grant. APPLY rehashes the whole
envelope but gives the bounded writer only the sealed raw-ROM subspan, so
authentication headers and signatures can never be written as firmware bytes.
No unauthenticated or substituted image can reach media through the internal
grant path.

The unselected synchronous transaction executor provides a composed path. It
keeps typed dispatch busy while it snapshots the protected owner record,
authenticates the exact staged intent, then reads the owner record again for
both CHECK and SET and rejects any interleaving change. Its durable checkpoint
requires that authenticated sequence and passes the read-back sequence plus the
same digest into the
broker's grant. The broker accepts APPLY directly from the still-staged intent;
no separately mutable APPLY message exists. SET closes the executor and broker
whether media succeeds or fails. CHECK completes dispatch without a checkpoint,
grant or APPLY.

Authentication is protected by a fail-closed in-progress latch established
before the first proof or hash callback. Nested authentication, checkpoint
grant and staged APPLY are rejected while it is set. Broker state is
revalidated after every callback boundary. A provider-triggered S3 close is
irreversible and causes the outer authentication to fail; the latch is cleared
on every ordinary success and failure return.

The provider reads current version and durable lowest-supported-version only
through the protected typed FMP owner. It verifies the authenticated-image CMS,
requires the signed MSS1 version to equal the staged attempted version, applies
the greater of the sealed platform floor and durable floor, and evaluates the
bounded dependency expression against the protected installed version. It then
requires exactly one valid FMAP, COREBOOT region and raw `build_info` file and
matches its vendor/part identity to the sealed board identity. Public coreboot
tables, payload build arguments and candidate metadata never select policy.

An `APPLY` additionally requires a one-use SMM-internal checkpoint grant for
the same generation, transaction and attempted version. Only the protected
variable owner may create that grant, and only after the combined FMP state has
been atomically committed with unsuccessful attempt status. The companion
internal checkpoint engine reads and commits through that same protected owner.
Every backend commit invocation, whether it reports success or error, is
followed by a fresh authoritative read while owner reentry remains blocked. An
exact candidate record is success even after an ambiguous callback error; the
exact prior record is failure, and unavailable, invalid, torn or any other
valid record fails closed. Callback mutation of the identity or either record
also fails closed. The checkpoint performs its own exact-state read after the
owner has reconciled that outcome.

The dormant generic owner journal is the first storage-side prerequisite, not
a platform backend. It binds one global epoch and all five fixed owner records
to one storage domain and the sealed identity set in a canonical manifest. An
abstract protected anchor names only an exact epoch and SHA-256 manifest
digest. Two erase domains hold append-only fixed slots. Recovery ignores
unanchored stale or newer records and accepts only the exact manifest named by
the anchor. Garbage collection first copies and verifies the authoritative
manifest in an erased alternate domain, then may erase its sole old copy. A
conditional anchor result is always reconciled by rereading both anchor and
manifest. A cleared anchor never initializes at runtime; a separate explicit
factory-provision operation is the only creation path. No anchor provider,
flash range or platform selection is supplied.

The journal media descriptor is pointer-free and canonical. Platform code must
fill it from immutable FMAP and update-route policy, never from a payload table
or build argument. Its `FMP_STATE_A` and `FMP_STATE_B` domains must be distinct,
inside the media, erase aligned, composed of two to 32 equal slots, and
disjoint from SMMSTORE and every allowed capsule route. The journal derives
its storage-domain binding by hashing that exact descriptor.

## Lifecycle

A malformed installation consumes the sole installation attempt. Typed
dispatch rejects malformed or stale intents before the executor can run. A
valid SET consumes its grant and closes the broker before hashing or media
access, so malformed APPLY state, digest, erase, write, read or verification
failure cannot be retried in the same boot. Every close, failed authentication,
rejected checkpoint grant and rejected APPLY clears retained raw-span state.
CHECK creates no grant or span
and leaves the path available for a later increasing transaction. S3 closure
is irreversible.

The production phase owner must close the broker before loading any external
EFI image, handing control to an OS or resuming from S3. The digest proves that
the bytes presented to SMM equal the coordinator's snapshot; it is not itself
authentication authority.

## Writer and status

The broker calls `capsule_apply_policy_verified()`. That writer completes all
plan and backend validation before its first media operation, accepts only the
immutable route policy, excludes SMMSTORE, touches only erase-aligned listed
regions and compares readback after each block. Every unlisted byte is
preserved. The sealed internal policy also carries both owner-journal domains;
the shared layout validator rejects a route reaching either domain before the
first media callback.

The writer currently returns only success or failure. The future selected
coordinator must map that result into FMP status without inventing device-
specific values. A partial-media failure remains a reset/recovery case.

## Gate

| Broker gate | Status |
| --- | --- |
| Endpoint/staged-intent ABI and sealed state machine | Implemented, unselected |
| Hostile O0/O2/ASan/UBSan model | Implemented |
| Trusted endpoint/policy producer | Open |
| Protected authentication-provider port | Implemented, unselected |
| Protected authentication provider | Implemented, unselected |
| Platform policy producer and trust anchors | Open |
| Typed checkpoint engine and grant ordering | Implemented, unselected |
| Synchronous staged-intent transaction executor | Implemented, unselected |
| Generic two-domain owner journal | Implemented, unselected |
| Canonical FMAP owner layout and writer exclusions | Implemented, unselected |
| Protected monotonic anchor and media backend | Open |
| DMA-protected staging and SMM rendezvous | Open |
| Fixed typed transport dispatcher | Implemented, unselected |
| SMI entry and endpoint publication | Open |
| Production SPI backend | Open |
| CDK2 composition and close-before-external-code proof | Open |
| QEMU power-loss and Intel/AMD hardware evidence | Open |

`Q35_CAPSULE_BROKER_TEST_PROOF` only compiles the SMM object. It publishes no
record, registers no SMI command and supplies no checkpoint, hashing or flash
provider. Host evidence is provided by:

`Q35_PAYLOAD_MM_OWNER_FMAP_TEST_PROOF` additionally selects a dormant QEMU
image layout with two 64 KiB preserved domains, each containing sixteen 4 KiB
slots. It still installs no owner, broker, transport or SMI entry.

```
tests/lib/capsule_broker_test.sh
tests/lib/capsule_broker_transport_test.sh
tests/lib/payload_mm_fmp_checkpoint_test.sh
tests/lib/payload_mm_fmp_auth_policy_test.sh
tests/lib/payload_mm_fmp_transaction_test.sh
tests/lib/payload_mm_fmp_owner_layout_test.sh
```
