# Payload-MM MOR bounded clear executor

`PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR` builds a dormant generic executor. It
adds no boot hook, platform selection, SMM transport, or MOR support claim. A
platform must separately establish cold-boot freshness, a complete immutable
DRAM inventory, protected DMA policy, and safe physical mapping and cache
operations. It must keep callback context and scratch physical backing outside
every `CLEARED` span in the supplied plan.

The executor snapshots and validates the canonical clear plan, MOR entry, and
operations table before its first callback. It ignores excluded spans. Each
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

DMA snapshots are acquired through the callback before clearing and after full
readback. Both snapshots must be individually valid and byte-identical. The
executor derives exact per-span byte counts, constructs its own facts and
transcript, and publishes a transcript and completion grant only through the
existing receipt builder. It accepts no producer boolean, count, transcript,
receipt, or grant assertion.

All public objects must be aligned, nonoverlapping native objects. Mapped host
ranges must not alias public objects or the executor's live immutable state.
Inputs and the operations table are checked again before publication, and both
outputs must remain zero until publication. Any malformed input, callback
failure, bad mapping, nonzero readback, DMA change, or mutation leaves both
outputs zero. The physical mapping, cache, fence, and DMA facts remain trusted
platform callbacks; this generic mechanism does not prove their hardware
implementation.
