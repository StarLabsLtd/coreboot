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

Selecting the typed broker contract makes the legacy EFI capsule driver
unavailable and compiles the SMMSTORE full-flash modifier out. Unmodified
SMMSTORE variable commands remain scoped to the `SMMSTORE` FMAP region. This
is a build-time route exclusion, not proof of the platform runtime-isolation
gates below.

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

### Fixed-buffer allocation prerequisite

`CAPSULE_BROKER_FIXED_BUFFERS` provides allocation and lifetime ownership
without installing or advertising the broker. The QEMU Q35 proof carves one
contiguous page-aligned reservation immediately below TSEG by lowering the
coreboot CBMEM ceiling and adding the exact carved range to the northbridge
resource map. The communication page and staging range are then derived from
that immutable boundary in every stage. Before the `LB_MEM_*` map is
serialized, coreboot requires both complete subranges to be `BM_MEM_RESERVED`.
Payload segment loading therefore cannot target them, and the operating system
sees them as reserved for the rest of the boot. Reservation fails closed if the
platform does not supply an aligned nonzero staging capacity, the address
calculation underflows, either range is not reserved, the CBMEM ceiling does not
equal the communication base, or the ranges overlap.

The communication allocation is one page, but only the fixed 168-byte transport
prefix is eligible for a future endpoint. The staging capacity is immutable
platform policy. The QEMU Q35 proof derives it from the configured ROM extent
plus one MiB for the bounded authentication and FMP envelope; neither address
comes from a PCD, payload build argument or caller pointer. The exact geometry
is copied directly into the protected SMM module parameters before lock, never
reconstructed from a public table. Both complete allocations are zeroed before
their geometry becomes available and the SMM owner provides an explicit full
scrub operation. Protected writer scratch consists of distinct page-aligned
write-snapshot and readback arenas in the SMM module. Both are zeroed on their
one permitted acquisition and explicitly scrubbed after use.

This is deliberately not a DMA claim. The option does not provide a
`dma_protected` proof, set `LB_CAPSULE_ENDPOINT_DMA_PROTECTED`, install a broker,
register an SMI or publish either capsule table record. A later composition must
independently isolate the exact communication and staging reservations from DMA
and keep that isolation through writer completion before it may install or
publish the endpoint.

The separate default-off QEMU `Q35_CAPSULE_DMA_TEST_PROOF` option supplies a
test-only `dma_protected` callback for the fixed geometry.  It rereads VT-d,
PMR, immutable table, enabled-requester and BME state and performs real denied
EDU writes into both allocations plus a mapped positive control on every call.
It does not install this policy, set the endpoint flag, register an SMI or
publish an endpoint; those remain composition responsibilities.

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
record is not authority. The dormant publisher emits the exact capsule handoff
and endpoint together, and accepts them only with an exact
valid capsule handoff and firmware identity, and only after a trusted platform
readiness provider proves the same sealed endpoint. It repeats that proof while
serializing the coreboot table over the exact endpoint, handoff and firmware
identity, and reruns both strict validators immediately before emission. Thus a
stale proof, retained-record mutation, S3 closure or any failed prerequisite
emits no record. Installation and serialization are one-shot;
callback mutation and reentry fail closed. The provider is context-free and
must consult platform-global sealed state; no transient pointer survives from
installation to table serialization. The strict endpoint validator is shared
unchanged by ramstage publication and the protected SMM policy install.

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
request. The dormant SMM route wrapper accepts only the byte-wide APM port and
command already sealed in the broker policy, rechecks the complete broker DMA,
SPI, communication-range, staging-range, raw-flash-exclusion and CPU-rendezvous
proof, then calls this existing dispatcher. Wrong port or command, stale
generation, malformed transport, reentry and S3 closure cannot reach the
executor. No platform registers this wrapper, supplies the readiness provider
or selects the endpoint capability in this tree.

Transport revision 2 adds one read-only `READ_INFO` operation without changing
the 40-byte request, 168-byte communication range or revision-1 `EXECUTE`
layout. Legacy revision-1 `EXECUTE` remains accepted byte for byte; revision 2
may use that same operation or request INFO with zero intent bytes and a fixed
128-byte response occupying offsets 40 through 167. The INFO completion marker
is therefore the same final word at transport offset 164 used by EXECUTE.
`READ_INFO` is rejected at revision 1, with a nonzero intent size, or with any
response size other than 128 bytes. INFO and EXECUTE have independent strictly
increasing transaction domains, so inspection cannot consume an update
transaction.

INFO has a separate, one-attempt policy install contract. Trusted platform
initialization must source the running image GUID, hardware instance, current
version, version floor and image size from authenticated current-firmware
identity and seal the complete policy in protected SMM storage. Capabilities
must exactly describe the bounded preserve-unlisted/readback-verifying broker;
the shared request cannot choose them. No platform in this tree installs that
policy. On each read, SMM rereads the protected owner state and requires its
durable version not to predate the sealed running version. Only the durable
floor and validity-qualified last-attempt fields come from that state. The
reported current version always comes from the sealed platform policy, never
solely from a mutable owner record. Corrupt, stale, unavailable or callback-
mutated state returns a bound failure response with all identity fields zero.
The fixed response has explicit last-attempt validity bits; an absent attempt
is not fabricated as a successful update. Mutation of the sealed INFO policy
or its lifecycle during an owner callback wipes that policy and permanently
poisons the one installation attempt; later reads cannot invoke the owner.

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

The hidden platform-facts prerequisite has no route, authority or publication
side effect. In SMM it takes one fail-closed snapshot of the real boot-medium
and SPI geometry, a bounded copy of the complete serialized FMAP inventory,
the same firmware identity used by the coreboot table, the fixed broker
communication and staging reservations, and two distinct SMM-owned scratch
blocks. Each scratch block must be exactly one hardware erase block; the
current fixed reservation therefore rejects non-4 KiB erase geometry. The
communication, staging and scratch reservations are pointer-free, bounded and
pairwise disjoint. FMAP areas may be nested or have an exact parent/child span,
but partially overlapping areas are rejected.
The collector neither interprets FMAP nesting as an update route nor invents a
broad COREBOOT write span: a later reviewed composition must partition the
authenticated full-media image around every immutable, preserved and SMMSTORE
span before it can install policy or publish an endpoint. No platform selects
this prerequisite here.

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

With the default-off TPM anchor-grant prerequisite, a compatible sidecar adds
a reset-safe split path without changing that manifest or anchor ABI. SMM first
writes and verifies the candidate while the old anchor remains authoritative,
then records one exact `PREPARED` generation, transaction and current/candidate
anchor tuple. After a future trusted pre-OS owner advances and reads back the
TPM anchor, releases the TPM and installs the matching one-shot grant, SMM can
consume the grant and reconcile only that tuple before marking it `COMMITTED`.
Prepared candidates are not returned by recovery. Power loss leaves either the
old manifest authoritative, a resumable prepared tuple, or an exactly
committed candidate; stale, replayed or mismatched grants fail closed. This
adds no TPM call in SMM and no producer, SMI, publication or platform choice.

TPM2 support does not currently satisfy that protected-anchor contract. The
bounded `NV_ReadPublic` helper can inspect a pre-provisioned index's exact
algorithm, attributes, authorization policy and size, but the existing TLCL
NV read and write helpers authorize an empty platform-password session. The
StarLabs Lite ADL and StarBook MTL configurations define neither a dedicated
index nor an ownership, policy-session or factory-provisioning contract for
one. Consequently no TPM anchor is selected and there is no fallback to an
ordinary flash or RAM anchor. A production provider must first add a sealed
authorization mechanism, verify the public metadata on every installation,
define monotonic and power-loss behavior, and require explicit external
provisioning; normal boot must never define, clear or repair the index.

The dormant capsule TPM anchor descriptor records the exact externally
provisioned NV handle, SHA-256 authorization policy, 40-byte anchor size,
authority key Name and policy reference. Its validator accepts only
`POLICYWRITE | WRITEALL | WRITE_STCLEAR | AUTHREAD | NO_DA |
PLATFORMCREATE`; in particular, no platform, owner or empty-index-password
write path is permitted. It compares the descriptor with a fresh bounded
`NV_ReadPublic` result and clears its copied binding on every mismatch or TPM
failure. A valid live index must report `WRITTEN`; `WRITELOCKED` is accepted
and copied so the caller can distinguish the pre-lock and post-lock phases,
while `READLOCKED` and every other unexpected dynamic attribute fail closed.
The descriptor has no default instance, index, board selection,
provisioning path, secret, policy-session implementation or runtime endpoint,
and validating it does not install an anchor provider.

The TPM2 policy-session transport is likewise dormant. It supplies bounded
wire primitives for an unsalted, unbound policy session and the assertions
needed to construct a policy transcript. The caller supplies every nonce,
digest, comparison operand and authorization bytes; the transport chooses no
index, policy, password or secret. `PolicyNV` accepts only the canonical
password authorization encoding and requires an empty response authorization;
HMAC and policy authorization require future state and response verification.
Every bounded success-code `StartAuthSession` response whose actual bytes
contain a complete valid policy handle is flushed when later validation fails,
even if its tag, declared size or nonce framing is malformed. Transport
failures, oversized responses and incomplete or invalid handles are never
salvaged. Its scoped callback snapshots the created handle, always attempts to
flush it, and reports a flush failure even when the callback also failed.
External-key tickets, authorized NV mutation and a capsule provider remain
separate dependencies.

The companion external-key transport accepts only caller-supplied RSA-2048,
RSA-3072 or RSA-4096 public moduli and RSASSA-SHA256 signatures. It constructs
one unrestricted signing public area with SHA-256 Name algorithm and the TPM
canonical zero encoding for exponent 65537, recomputes the returned Name, and
uses the owner hierarchy so a successful verification produces a usable
owner-hierarchy ticket. Ticket digests are opaque context-integrity values;
the transport accepts the bounded SHA-1, SHA-256, SHA-384 and SHA-512 digest
sizes rather than assuming the RSA scheme selects their length. Loaded
objects are transient and the scoped helper always flushes its snapshotted
handle. Policy-authorized NV write and write lock request continuation only so
a future provider can deterministically flush the policy session. The
transport API does not enforce terminal use; a provider must invoke either
mutation only as the final operation inside `tlcl2_policy_session_run()`. The
returned nonce is validated and discarded, and this interface is not a
reusable HMAC-session state machine. The transport embeds no public key,
signature, policy digest, policy reference, NV index or provider choice, and
it does not install or execute a capsule policy.

The offline `util/capsule_tpm_provision` utility deterministically generates
and validates review artifacts for initial factory provisioning. It accepts an
explicit full NV handle, initial anchor, RSA authority modulus and policy
reference, and never opens a TPM or accepts a private key, signature, password
or ownership secret. Its static policy contains separately command-bound
`NV_Write` and `NV_WriteLock` branches after `PolicyAuthorize`, joined by one
canonically sorted `PolicyOR`. The initial write authorization binds
`PolicyNvWritten(false)` and the exact complete-write cpHash; the initial lock
authorization separately binds `PolicyNvWritten(true)` and its exact lock
cpHash. The utility does not provision the index or generate update
authorization. A future updater must additionally bind
`PolicyNvWritten(true)`, `PolicyNV` equality against the exact current 40-byte
anchor, and the exact replacement-write cpHash before authorization. Thus the
static policy can support a ratchet without creating an arbitrary-write path,
but no such provider is selected or executable yet.
Because the exact index attributes include `PLATFORMCREATE`, factory definition
must use explicit platform-hierarchy authorization; owner-hierarchy definition
is not compatible with the generated public area.

The journal media descriptor is pointer-free and canonical. Platform code must
fill it from immutable FMAP and update-route policy, never from a payload table
or build argument. Its `FMP_STATE_A` and `FMP_STATE_B` domains must be distinct,
inside the media, erase aligned, composed of two to 32 equal slots, and
disjoint from SMMSTORE and every allowed capsule route. The journal derives
its storage-domain binding by hashing that exact descriptor.

A dormant generic media adapter maps only those two validated `FMP_STATE_A`
and `FMP_STATE_B` ranges onto writable `region_device` children of one media
root. Reads, programs and erases must complete exactly within one domain;
programming rejects every attempted zero-to-one transition, and erase uses the
descriptor's exact geometry. Short or failed writes and erases are ambiguous
until authoritative readback proves the requested bytes. Durability is a
separate mandatory callback: no absent operation or successful return from a
memory-backed `region_device` is treated as a flush. The copied layout,
subregions, callback and callback context are sealed, and mutation or reentry
fails closed; policy mutation poisons the adapter. This supplies no protected
anchor, journal installation, platform selection or production SPI authority.
The protected adapter object starts zeroed and accepts exactly one
initialization attempt; malformed or repeated initialization poisons it.

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
regions, durably synchronizes and compares readback after each block. Every
unlisted byte is preserved. The sealed internal policy also carries both
owner-journal domains and the complete coreboot-derived FMAP area inventory.
Each update route must exactly match one writable FMAP area and cannot overlap
any static, read-only or preserved area. The shared layout validator rejects a
route reaching either journal domain or SMMSTORE before the first media
callback.

Immediately before APPLY, the existing protected broker snapshots a
pointer-free flash plan from its exact checkpoint grant: generation,
transaction, owner sequence, version, capsule digest, authenticated raw-image
span and immutable routes. This is not a second planner authority or replay
counter. The broker closes and clears the grant before execution; mutation or
reentry is detected by the same control-state guard used throughout the
one-shot broker.

The raw image remains in the broker's fixed DMA-isolated staging allocation;
there is no second full-image copy in SMRAM. Before each destructive block the
writer proves the exact source pointer and length and rechecks DMA isolation,
SPI ownership and CPU rendezvous, then copies one erase block into protected
write scratch. It rechecks the guard before erase and write, synchronizes, and
reads into a separate protected scratch block for comparison with the write
snapshot. Both scratch blocks, all copied callback contexts, communication and
staging are pairwise disjoint. Media callbacks are trusted platform backend
code; no callback context aliases staging or either scratch block.
Both scratch allocations are exactly one erase block, bounding protected-memory
use and the final wipe latency.

The flash plan intentionally does not duplicate board identity or image GUID.
The trusted authenticator verifies both against the exact whole-capsule bytes;
that whole-capsule digest and its authenticated raw-image span are retained in
the checkpoint grant. APPLY rehashes the exact whole capsule from protected
staging before the broker-owned plan consumes that span and its immutable
routes. Thus the plan is transitively bound to both identities without a
second mutable authority. Host tests exercise this chain through
`authenticate-image-mutation`, `bound-digest`, `bound-mutation`, and the
coordinated generation, transaction and version mutation cases.

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
| Generic owner journal `region_device` media adapter | Implemented, unselected |
| Protected monotonic anchor and media backend | Open |
| DMA-protected staging and SMM rendezvous | Open |
| Fixed typed EXECUTE/INFO transport dispatcher | Implemented, unselected |
| Authenticated current-image INFO policy producer | Open |
| Legacy SMMSTORE full-flash route exclusion | Implemented |
| SMI entry and endpoint publication | Open |
| Production SPI backend | Open |
| CDK2 composition and close-before-external-code proof | Open |
| QEMU power-loss and Intel/AMD hardware evidence | Open |

`Q35_CAPSULE_BROKER_TEST_PROOF` only compiles the SMM object. It publishes no
record, registers no SMI command and supplies no checkpoint, hashing or flash
provider. Host evidence is provided by:

`Q35_PAYLOAD_MM_OWNER_FMAP_TEST_PROOF` additionally selects a dormant QEMU
image layout with two 64 KiB preserved domains, each containing sixteen 4 KiB
slots, and compiles the dormant media adapter. QEMU memory-pflash can exercise
`region_device` behavior but is not production SPI or durability authority. It
still installs no owner, broker, transport or SMI entry.

```
tests/lib/capsule_broker_test.sh
tests/lib/capsule_broker_transport_test.sh
tests/lib/capsule_broker_info_test.sh
tests/lib/smmstore_full_flash_exclusion_test.sh
tests/lib/payload_mm_fmp_checkpoint_test.sh
tests/lib/payload_mm_fmp_auth_policy_test.sh
tests/lib/payload_mm_fmp_transaction_test.sh
tests/lib/payload_mm_fmp_owner_layout_test.sh
```
