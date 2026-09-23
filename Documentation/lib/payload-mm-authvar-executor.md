# Payload-MM authenticated-variable executor

The executor is an internal SMM-only bridge between the authenticated-variable
writer plan, the FTW decoder, and the sealed media port. It does not expose raw
flash, geometry, plan, or offset controls to a payload or SMI caller. The Q35
proof option only builds the object for validation; it installs no
endpoint or production backend.

## Protected arena and lifetime

Installation is one-shot. The arena, limits, executor state, media-private
state, and request inputs must be protected SMRAM and mutually disjoint. Layout
arithmetic uses the toolchain's maximum ABI alignment and rejects overflow or
insufficient space. A single-flight lock gives one operation exclusive use of
the arena.

The arena holds one full SMMSTORE snapshot, bounded scanner and reclaim arrays,
one canonical record, copied name and data, a 4 KiB transfer buffer, and session
metadata. Small pointer-free control metadata is copied exactly and compared
around each media call. No executor-arena pointer crosses the media interface:
PR143 copies backend-visible buffers through media-private protected scratch.
Arbitrary writes by same-privilege SMM code to unrelated protected memory are
outside this portable contract; such code could equally corrupt a stack or a
comparison key. Deterministic checkpoints instead rebuild or freshly read and
validate every durable and logical output before it becomes authoritative.

All source, index, plan, snapshot, generation, and token state expires at the
one mandatory media end. Session metadata and every session-scoped live pointer
are cleared after the operation; the remaining arena bytes have no surviving
session owner or reachable interface. A media session begins before the
authority snapshot so the contract is bound to that session. Cache state is invalidated immediately
after begin and bound only after a fresh final read, FTW decode, store scan,
and logical comparison.

## Offset and I/O contract

Decoder geometry offsets are complete-SMMSTORE-relative. Writer offsets are
authenticated-variable-store-relative and are translated only by adding the
decoder-validated FV header size. The current format requires
`geometry.variable_offset == 0`; any other value is an invariant failure.
Every backend transfer is at most 4 KiB. Erases use the sealed erase geometry.

Direct records are programmed with their state byte excluded from body calls;
state transitions are separate exact-byte read/NOR/program operations. FTW
workspace, write-header, and write-record commit bytes are likewise excluded
from body writes. The shared FTW header owns their sizes, state vocabulary,
working-block GUID, and coreboot caller GUID so producer and decoder cannot
drift.

## Reclaim and recovery

Reclaim first builds the complete replacement FV in protected memory and scans
it strictly. It proves that every old live winner except the replaced key is
copied exactly once and that the canonical new winner is at the planned end.
Only then does it emit the EDK2-compatible `fe/fc/fd/f9/f8` sequence. Each body
and the full spare, including erased surplus, is read back before its next
marker. Primary replacement is also compared in full.

Recovery repeatedly reads the complete store, decodes one typed action, applies
only that action, and rereads. It supports workspace initialization/reclaim,
typed stale-spare cleanup, abort, replay, completion, and staged-workspace
restore. Repeated plans or the fixed iteration limit are contradictions.

Ordinary backend errors before an ambiguous mutation return their mapped
status and leave a reset-safe durable prefix. Malformed committed state,
impossible plans, marker/readback mismatches, control mutation, or
non-convergence call media fail-close with the immutable session owner and then
perform exactly one sealed end. End failure invalidates any cache binding and
turns an otherwise successful operation into `DEVICE_ERROR`.

## Dormant read transaction

The executor also exposes an internal SMM-only GET, NEXT and QUERY transaction.
It uses the same nonwaiting gate, media session, FTW recovery and fresh store
scan as SET; it is not a parallel reader, authority path, wire endpoint or
cache. Reads neither require nor invoke the policy provider and do not advance
READY_TO_BOOT or runtime state.

Request and result descriptors, the input cursor, and bounded output name/data
spans must be protected, mutually disjoint and outside executor and
media-private storage. The cursor is copied before media access. Store-helper
entries never escape: selected bytes and scalar metadata are staged in the
protected arena and caller buffers are published only after the mandatory
media end succeeds. No caller output span is exposed to the media port or
policy provider. Completion is stored last with release ordering. Success
zeroes unused output capacity. `BUFFER_TOO_SMALL` publishes only the required
size, plus GET attributes, and no partial bytes or NEXT GUID. Other semantic
failures publish no payload metadata. Media-end failure publishes only
`DEVICE_ERROR` and leaves caller byte buffers untouched. The complete arena is
scrubbed before releasing the gate.

A busy or provider-recursive call fails without modifying its result or output
spans. The single-flight gate remains held through result publication and the
release-store of COMPLETE; it is released only after the caller can observe a
complete result. Media-begin failure also leaves caller byte outputs untouched,
because no media session existed whose successful end could authorize output
publication.

GET and NEXT use committed winners from the freshly scanned index even when a
later torn record leaves a dirty tail. QUERY rejects that tail with
`DEVICE_ERROR`, because uncommitted physical bytes cannot safely contribute to
quota accounting; a later mutating recovery/reclaim must canonicalize it.
QUERY quotas come only from sealed executor limits and decoded store geometry,
never caller input. Although GET, NEXT and QUERY perform no logical variable
write, the shared recovery path may repair or complete an interrupted durable
FTW transaction before reading.

This slice installs no shared-memory descriptor, dispatcher, SMI route or
persistent read cache. The dormant service-frame validator remains a separate
composition boundary and must be aligned with EDK2 QUERY APPEND and unknown-bit
behavior before any endpoint is published. Its current QUERY response rules
also cannot represent the `DEVICE_ERROR` returned for dirty-tail quota state.
