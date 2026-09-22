# Authenticated-variable append and state planner

`PAYLOAD_MM_AUTHVAR_WRITER` is the dormant SMM-only planning boundary between
authenticated-variable policy and the future FTW executor. It builds the
canonical 60-byte authenticated-variable header, UTF-16 name, aligned data and
padding without importing EDK2 types or build machinery. The source behavior is
pinned to StarLabsLtd/edk2 `26.09` at
`aab7b589fc59b7e2b8fb7eb79519bf1a5e5a5272`.

This is an internal, non-SMI helper. Gate G must acquire the media session,
bind a fresh read/scan to its stable generation, and place immutable inputs and
mutually disjoint outputs in protected SMRAM before invoking it. Planning from
normal-world pointers or copying the result into protection afterward is not a
supported call pattern.

For an in-place append opportunity, the plan retires an older transition
record, moves the current record to `VAR_IN_DELETED_TRANSITION`, installs the
complete new body while its state byte remains erased, programs and verifies
`VAR_HEADER_VALID_ONLY`, then programs and verifies `VAR_ADDED`, then deletes
the replaced record. Deletion retires an older transition before the selected
record. EFI append data is merged into the canonical output record and the
transient append attribute is never persisted.

The extra `0xff -> 0x7f -> 0x3f` markers are a compatible hardening over the
pinned EDK2 implementation's direct `0xff -> 0x3f`: each boundary clears one
NOR bit; the later executor must program and verify each marker and does not
assume that either program is atomic. A complete erased-state record is
consumed but invisible. A torn erased-state tail is recorded by the scanner as
dirty, consumes the remaining append space, and therefore forces reclaim
rather than being overwritten.

Every record and step offset is authenticated-variable-store-relative. Gate G
adds the decoder-validated FV header translation. Each state step requires an
exact expected-state comparison, a NOR-safe compare-and-clear transition,
durable program and readback before the next step. Direct tail append uses this
ordered state machine and is intentionally not an FTW reclaim transaction.
The RECORD step first verifies that its exact `offset`/`size` span is erased,
programs exactly `plan.record_size` canonical bytes with the state byte still
`0xff`, and requires durable byte-for-byte readback before the following
`0xff -> 0x7f` state step. A compare, program, or readback failure stops the
plan; no later step may run.

If compacting is required, the writer returns the existing deterministic
reclaim plan and emits no state steps. `reclaim.compacted_used_size`,
`reclaim.copy_count`, every `reclaim.copies[]` entry, the canonical record,
`plan.record_offset` (equal to `reclaim.destination_offset`) and
`plan.record_size` are the complete input for Gate G's spare-image construction
and journaled FTW transaction. It never calls the media port, changes a cache,
or assumes that a flash program is atomic. The
protected snapshot, source, canonical record, reclaim plan/copies and write plan
are one immutable execution bundle bound to one active media generation and
session; all become invalid at `media_end()`. Recovery at every boundary must
be proved before any variable-service endpoint is exposed.

Gate G must also status-gate this API: every output is invalid after any
non-success return and must never be inspected or executed. In particular, an
alias error cannot safely clear an output that overlaps a caller-owned input.

The writer supports only the persistent common-variable store; volatile and
hardware-error classes return unsupported. It enforces the same name, data,
record-byte, physical-record and logical-entry limits as the next scan. Hitting
the physical-record limit forces
boot-time reclaim even when byte space remains and returns out-of-resources at
runtime. A dirty-tail index must likewise be reclaimed before QUERY or any
other service request is dispatched.

The Q35 proof merely compiles this dormant planner. It installs no backend,
dispatcher, SMI route, endpoint, or production authority.
