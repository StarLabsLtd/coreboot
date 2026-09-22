# Payload-MM authenticated-variable media port

`PAYLOAD_MM_AUTHVAR_MEDIA_PORT` is a default-off, SMM-only adapter between the
sealed authenticated-variable contract and a platform-owned SMMSTORE backend.
Installation is one-shot and requires the authority to be installed first. It
uses that sealed authority directly to prove that the descriptor, complete
context, every callback entry point, and destination owner state are in SMRAM;
there is no caller-supplied protection proof. It then snapshots the contract,
derives the EDK2 variable, working and spare spans with the FTW geometry decoder,
and copies the immutable callback descriptor and bounded context into storage
with the toolchain's maximum ABI alignment. Callback
offsets are relative to the complete SMMSTORE region, never raw boot-media
addresses. The existing FMP owner-layout validation keeps its state and update
ranges disjoint from SMMSTORE; this port cannot express an offset outside that
sealed child, so it cannot cross into an FMP-owned range.

One successful `begin()` holds global backend/SPI exclusion until `end()`. The
nonzero backend generation is stable for that session and must advance after an
out-of-band media change. A private owner token prevents a stale caller from
ending or using another session. Cached scan/plan state is reusable only while
its backend generation still matches and is invalidated before every mutation.

Programs are bounded to the protected 4 KiB scratch capacity. The adapter
snapshots the source, pre-reads the old bytes, rejects NOR 0-to-1 transitions,
calls the backend once, syncs, and compares an exact readback. Erase accepts one
decoder-authorized, aligned erase block only; the erase block must also fit the
4 KiB protected snapshot capacity. It snapshots the whole block and accepts
only an all-`0xff` readback. A write-protected result is returned only when the
readback proves that the media is byte-for-byte unchanged. Partial or ambiguous
mutation permanently poisons the port.

The SMM-internal buffer-disjoint predicate exposes only a boolean result. It
rejects null, empty, wrapping, and every exact or partial overlap with the
media policy, copied contexts, session state, or scratch buffers. It is safe
before port installation and before a media session, and does not reveal a
private address or prove that a disjoint buffer is otherwise trusted.

An internal executor which detects a higher-level invariant failure can call
`payload_mm_authvar_media_fail_closed()` with its active generation and owner
token. The call invalidates every cache binding and permanently poisons the
installed port, but deliberately retains backend exclusion. The session owner
must still call `end()` exactly once; the sealed backend cleanup runs and the
wrapper returns a device error. No later session or mutation is accepted for
the remainder of the boot. An invalid owner or a call from inside a backend
callback is also a terminal fail-closed event. This primitive is not an SMI or
normal-world API and does not expose a media operation.

This slice has no variable-record writer, state transition, FTW executor,
recovery action, SMI dispatcher, service endpoint, backend installer, or table
publication. The Q35 config is a compile proof only. It does not establish the
production requirements for SMM-only SPI ownership, lock readback, DMA-safe
staging, or removal of generic raw/full-flash SMMSTORE routes.
