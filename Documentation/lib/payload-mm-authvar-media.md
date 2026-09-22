# Payload-MM authenticated-variable media port

`PAYLOAD_MM_AUTHVAR_MEDIA_PORT` is a default-off, SMM-only adapter between the
sealed authenticated-variable contract and a platform-owned SMMSTORE backend.
Installation is one-shot and requires the authority to be installed first. It
uses that sealed authority directly to prove that the descriptor, complete
context, every callback entry point, and destination owner state are in SMRAM;
there is no caller-supplied protection proof. It then snapshots the contract,
derives the EDK2 variable, working and spare spans with the FTW geometry decoder,
and copies the immutable callback descriptor and bounded context. Callback
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

This slice has no variable-record writer, state transition, FTW executor,
recovery action, SMI dispatcher, service endpoint, backend installer, or table
publication. The Q35 config is a compile proof only. It does not establish the
production requirements for SMM-only SPI ownership, lock readback, DMA-safe
staging, or removal of generic raw/full-flash SMMSTORE routes.
