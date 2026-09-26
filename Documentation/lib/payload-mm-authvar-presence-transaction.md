# Payload-MM authenticated-variable presence transaction

`PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION` builds a dormant private transaction
that keeps the authenticated-variable presence authority gated until public
endpoint publication owns its irreversible finalization boundary. It is hidden,
defaults off, has no board selection, public command, APM route, table writer,
or platform dispatch registration.

The ramstage producer creates a fresh generation-bound transaction identifier,
nonce and private capability. Its protected transport returns two independent
results: an exact authenticated acknowledgement in a dedicated reserved 4 KiB
page and an exact saved-RAX result from the evidenced initiating CPU. `PREPARE`
installs the authority but leaves public dispatch gated. A publication owner
then wins the sole `PREPARED` to `FINALIZING` transition. It completes every
fallible proof and record preparation before issuing `COMMIT`. Only an exact
commit acknowledgement permits the local committed endpoint record. An abort
that wins first exact-restricts the same generation and prevents publication;
a stale abort cannot close finalizing or committed authority.

The protected receiver accepts a loader-provisioned fixed slot and one-shot
exact-`BM_MEM_RESERVED` receipt for the transaction page. The receipt is copied
to protected storage and consumed only by the unique dispatch owner. The page
is never dereferenced until the verified snapshot establishes its exact base,
4 KiB size and tag, and a trusted callback proves complete DMA protection.
Before any authority callback, another trusted architecture callback claims one
private invocation, seeds and reads back the reserved RAX sentinel, identifies
the unique initiating BSP, reports the actual active CPU count, and supplies an
opaque proof that every active CPU joined the same SMI generation. Generic code
does not infer these facts from `CONFIG_MAX_CPUS`, an SMM lock, or handler CPU.

`PREPARE`, `COMMIT`, and `ABORT` bind revision, size, generation, transaction
identifier, nonce, CPU facts, capability, decision, transport status, operation
status, and zero-reserved fields. The receiver uses explicit `FINALIZING` and
`COMMITTING` phases. A commit callback or saved-state acknowledgement that may
have taken effect but cannot be proved invokes the platform's nonreturning
fail-stop path and is never retried or rolled back. Failed or ambiguous prepare
is exact-aborted once; ambiguous abort also fail-stops.

Before release-publishing `COMMITTED` or `ABORTED`, the sole owner scrubs the
raw capability, authority callbacks and context, receipt verifier and receipt,
page metadata, and the complete authenticated request page. It then copies the
already-built final acknowledgement into the page with no further fallible
work. Terminal requests are rejected before any invocation callback or page
access; there is deliberately no terminal replay. Public authority dispatch
requires an acquire-loaded `COMMITTED` state and the final acknowledgement
publication gate.

## Deliberate integration blockers

This slice does not provide the platform-private SMI cause, SMM dispatcher,
fixed protected slot accessor, all-active-CPU rendezvous, saved-state access,
reset/fail-stop implementation, transaction-page reservation signer, or board
composition. The existing handoff remains independent. A later platform slice
must provision a dedicated below-4-GiB reserved page and independent exact-tag
receipt authority before permanent SMM loading, emit its receipt after both
bootmem maps resolve, and connect only a recognized private cause. Until those
pieces exist, no platform can select or reach this transaction.
