# Payload-MM MOR bounded clear executor

`PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR` builds a dormant generic executor. It
adds no boot hook, platform selection, SMM transport, or MOR support claim. A
platform must separately establish cold-boot freshness, a complete immutable
DRAM inventory, protected DMA policy, and safe physical mapping and cache
operations. It must keep callback context and scratch physical backing outside
every `CLEARED` span in the supplied plan.

The executor snapshots and validates the canonical clear plan, MOR entry, and
operations table before its first callback. The operations table includes a
separate context and allocation-free live-inventory validator. That validator
must reconstruct or otherwise prove that the supplied canonical plan still
describes the complete live DRAM inventory; an orchestrator-only prior check is
not sufficient. It ignores excluded spans. Each
cleared span is processed through bounded physical windows, so addresses above
4 GiB do not depend on host pointer width. A mapping request never crosses a
`window_bytes`-aligned physical boundary, so each callback receives exactly one
physical window even when a cleared span starts unaligned. For every window it
performs each volatile zero write itself, invokes cache writeback and a
completion fence, and unmaps. Only after all writes are durable does a separate
pass map each window, writeback-invalidates and fences it, volatile-reads every
byte, requires zero, and unmaps it. Every successful map is
matched by an unmap attempt, including error paths. A failed map must leave no
active mapping; a successful map must return exactly the requested number of
accessible bytes. A map failure must set its output to `NULL` and leave no
mapping to release. Cache writeback/invalidation is complete only after the
following fence.

The live inventory is validated immediately before the first DMA snapshot, and
again after the final DMA snapshot and before receipt construction. An initial
mismatch therefore fails before a map or write. A late mismatch fails without
publishing a transcript or grant. The validator receives the const plan input,
returns status, and must not retain or modify it. The executor compares that
input with its private snapshot immediately after each validation.

DMA snapshots are acquired through the callback after initial inventory
validation and after full readback. Both snapshots must be individually valid
and byte-identical. The
executor derives exact per-span byte counts, constructs its own facts and
transcript, and publishes a transcript and completion grant only through the
existing receipt builder. It accepts no producer boolean, count, transcript,
receipt, or grant assertion.

All public objects must be aligned, nonoverlapping native objects. Mapped host
ranges must not alias public objects or the executor's live immutable state.
Inputs and the operations table, including both callback contexts, are checked
after each inventory validation and again before publication. Both outputs
must remain zero until publication. Any malformed input, inventory mismatch,
callback failure, bad mapping, nonzero readback, DMA change, or mutation leaves
both outputs zero. The inventory, physical mapping, cache, fence, and DMA facts
remain trusted platform callbacks; this generic mechanism does not prove their
hardware implementation.
