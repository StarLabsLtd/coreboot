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
