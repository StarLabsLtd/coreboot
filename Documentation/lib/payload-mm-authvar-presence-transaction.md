# Payload-MM authenticated-variable presence transaction

`PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION` builds a dormant private transaction
that keeps the authenticated-variable presence authority gated until public
endpoint publication owns its irreversible finalization boundary. It is hidden,
defaults off, has no board selection, public command, APM route, table writer,
or platform dispatch registration.

The ramstage producer creates a fresh generation-bound transaction identifier,
nonce and private capability. Its protected transport returns two independent
results: an exact authenticated acknowledgement in a dedicated reserved 4 KiB
page and an exact saved logical result from the evidenced initiating CPU. `PREPARE`
installs the authority but leaves public dispatch gated. A publication owner
then wins the sole `PREPARED` to `FINALIZING` transition. It completes every
fallible proof and record preparation before issuing `COMMIT`. Only an exact
commit acknowledgement permits the local committed endpoint record. An abort
that wins first exact-restricts the same generation and prevents publication;
a stale abort cannot close finalizing or committed authority.
The producer retains its sealed fail-stop composition in a short
`PUBLISHED_PENDING` phase until the final table owner consumes the exact
address-bound publication receipt after copying the endpoint.

The protected receiver accepts a loader-provisioned fixed slot and one-shot
exact-`BM_MEM_RESERVED` receipt for the transaction page. The receipt is copied
to protected storage and consumed only by the unique dispatch owner. Entry into
`PAGE_CONSUMING` is terminal: success advances to `PAGE_OWNED`, while failure
scrubs the verifier and receipt and invokes the sealed platform-wide fail-stop;
it never returns to retryable `PROVISIONED`. The page
is never dereferenced until the verified snapshot establishes its exact base,
4 KiB size and tag, and a trusted callback proves complete DMA protection.
Before any authority callback, another trusted architecture callback claims one
private invocation, seeds and reads back the reserved logical sentinel, identifies
the unique initiating BSP, reports the actual active CPU count, and supplies an
opaque proof that every active CPU joined the same SMI generation. Generic code
does not infer these facts from `CONFIG_MAX_CPUS`, an SMM lock, or handler CPU.

Provisioning proves the protected placement of the slot, policy, context,
receipt and verifier, plus the proof callback and every policy callback code
address. It snapshots the policy, binding, receipt and verifier before proving
callback code or context placement, then rejects any source change before or
after the ownership claim. An independent protected failure closure retains
only the platform fail-stop callback, generation and a pristine private context
copy. Dispatch validates and snapshots that closure before trusting the
ordinary policy; any later policy, binding, context, geometry, receipt or
closure mutation uses the retained copy. A structurally invalid closure traps
rather than calling an untrusted address.

`PREPARE`, `COMMIT`, and `ABORT` bind revision, size, generation, transaction
identifier, nonce, CPU facts, capability, decision, transport status, operation
status, and zero-reserved fields. The receiver uses explicit `FINALIZING` and
`COMMITTING` phases. A commit callback or saved-state acknowledgement that may
have taken effect but cannot be proved invokes the platform's nonreturning
fail-stop path and is never retried or rolled back. Failed or ambiguous prepare
is exact-aborted once; ambiguous abort also fail-stops.

Every successful invocation claim is closed exactly once only on a graceful
canonical result. The one-shot completion callback must publish the nonzero
saved logical value and fully close that exact invocation. It may return an error
only when it proves that no CPU or EOS was released; any shutdown race, partial
close or ambiguous evidence transition must fail-stop inside the provider.
Fatal receipt, DMA, token, callback or protected-state failures deliberately
retain the claimed invocation and all-CPU rendezvous until platform reset.

Before release-publishing `COMMITTED` or `ABORTED`, the sole owner clears the
acknowledgement gate, scrubs the authenticated request page, stages its canonical
acknowledgement, and scrubs the raw capability, authority callbacks and context,
receipt verifier and receipt, and page metadata. It exact-transitions to the
final but still hidden state, completes the claimed invocation as the last
fallible callback, release-publishes the acknowledgement gate, and only then
releases dispatch ownership. Terminal requests are rejected before any
invocation callback or page access; there is deliberately no terminal replay.
Public authority dispatch requires an acquire-loaded `COMMITTED` state and the
final acknowledgement publication gate.

The receiver requires `SMM_MODULE_STACK_SIZE >= 0x4000`. Its measured 32-bit
in-tree dispatch and receipt-verification chain is bounded below 4 KiB; a live
provider and handler integration must keep the complete chain below 12 KiB so
at least 4 KiB remains for emergency fail-stop handling.

## Deliberate integration blockers

The producer seals the transaction generation, ID, nonce and capability during
its pre-bootmem reservation, before permanent SMM loading. Its one-use loader
binding take fixes the real initiating CPU and active CPU count; composition
must reuse that identity and rejects a different topology. The take does not
install authority or establish receipt ownership. The actual SMM loader now
provisions a protected bootstrap record through the existing tuple-sender and
runtime-binding conjunction. It retains the same full loader nonce, genuine
cold classification, topology-bound transaction identity and independent
mailbox/page receipt verifiers. A disabled early publication decision leaves
the complete record zero; a required provisioning failure aborts loading.

`test-authvar-presence-canonical-bootstrap` checks the real provisioning code
with mocked external owner boundaries, including every provisioning failure,
disabled placement and replay. Its separate full-ROM lane links untouched MTL
loader/handler objects and the real board composition owner, with opt-in absent.
The optional `PRESENCE_BOOTSTRAP_AUTHORITY_POLICY_LINK=1` lane additionally
requires the real board authority-policy object. Its nested
`PRESENCE_BOOTSTRAP_PRIVATE_CALLER_LINK=1` lane checks the actual board-table
sender and private dispatcher/factory caller objects. These ROM lanes keep
opt-in absent; they do not assert required-true end-to-end boot.

The MTL caller uses the existing `lb_board` hook after both bootmem maps resolve
to emit the exact mailbox/page receipts and request the existing private 0xfe
all-CPU dispatcher. Non-bootstrap requests, including legitimate AP requests,
retain the ordinary lifecycle-close path. The recognized bootstrap imports
mailbox ownership through the existing backing-evidence owner and retains the
page receipt for canonical PREPARE. It retires the first save-state lease before
calling the existing route factory, then uses a second lease of the same ops
descriptor for the final response. The public frame remains REQUEST until the
factory succeeds and that second lease retires. APs remain held throughout.
Any intermediate failure invokes platform fail-stop without a completed ACK.

Host fixtures execute the actual sender, receiver and dispatcher with mocked
external owner/factory boundaries. A separate import fixture links the actual
bootstrap, backing-evidence, transaction and receipt/MAC implementation: it
checks exact mailbox ownership, generation, full loader identity, page-handle
binding, one-use/replay and alias rejection. These tests are not a live MTL
required-true service proof.

Production activation still requires genuine board composition opt-in and the
general variable-service/endpoint path, followed by integrated cold-DMA and
route/service runtime validation. PREPARE remains the sole presence-seed
installation boundary; there is no independent seed handoff. The existing S3
DMA epoch is deliberately not a cold-boot proof. The separate cold callback
uses the live DMA policy and the same protected receipt owner, but host and
link evidence do not replace hardware validation of that complete path.

The general-service prerequisite reserves one distinct, fixed 64 KiB
`BM_MEM_TABLE` mailbox only with the hidden fixed-dispatcher attestation and a
genuinely required canonical composition. Presence-only and disabled profiles
do not allocate it. Its exact post-map receipt is imported through the same
private bootstrap wave, after which the sole backend provider consumes it via
`payload_mm_authvar_service_prepare`. The canonical slot does not retain a
second service receipt or expose a raw receipt getter. Preparation establishes
mailbox ownership, not installed service or endpoint readiness.

That full prerequisite also defers the existing single SMRAM arena allocation
until the real canonical tuple has provisioned its protected cold binding.
The arena uses that binding's generation and capability owner, the same live
occupied-region list and the existing allocator. Full loader nonce, topology,
cold classification and bootstrap state are resampled before publication.
Failure closes all staged receipt owners and stops boot without falling back
to the independent legacy MOR owner. Without the full prerequisite, the
existing MOR-only arena path remains unchanged.

The host lane compiles the exact production loader functions with mocked
rmodule, fanout and transport boundaries and links the real arena allocator,
loader-instance and topology implementations. It covers both attestation
settings, required/disabled decisions, optional MOR and ownership drift; its
failure-atomic companion runs source mutations rather than raw statement
counts. The receipt-import fixture uses the real receipt/MAC implementation
but mocks the sole provider's preparation boundary. These are prerequisite
proofs, not an enabled production profile. The real fixed 0xfc dispatcher,
unconditional sole-backend initialization, optional MOR operation and final
service/endpoint readiness still require integrated source and runtime proof.

The guarded full-composition private wave now uses the existing invocation
entry ledger: every CPU arrives, the BSP claims the actual rendezvous, and
receipt import precedes installation. Retiring the first save-state lease does
not retire this invocation claim. The fixed platform admission callback checks
the protected dispatcher phase, retained ops identity, retired lease, full
loader nonce, topology and claimed token before and after backend callbacks.
The platform factory reuses the existing restricted Intel SPI callback rather
than installing a second media provider. Presence-only composition keeps its
previous path.

The threaded host wave fixture links the real entry, evidence, loader-instance
and topology implementations while mocking platform classification, save-state
adapter and receipt/factory boundaries. It tests delayed and AP-first arrival,
missing AP, callback reentry, token/identity/topology/lease drift, publication
failures and EOS denial. A post-ACK EOS failure is terminal: it is not an
unpublished response or a successful return to the payload. These proofs do
not select the production profile or prove the complete service backend link.
Optional MOR channel attachment still needs its later genuine private request
admission; copying its slot during this wave or reusing the expired claim is
not sufficient. Fixed 0xfc runtime dispatch, final service readiness and
endpoint publication remain separate gates.
