# Memory-overwrite Control clear transaction

`PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION` is a dormant SMM-only
composition primitive. It installs no dispatcher, SMI command, boot hook, or
support claim.

The transaction owns one authenticated-variable media lease from its initial
snapshot through final readback. It first simulates bounded FTW recovery into
the recovery planner's protected canonical image. It then validates the exact
MOR Control GUID, UTF-16 name, fixed attributes, one-byte data, and asserted bit
zero, and completely preflights the ordinary internal replacement record. The
record, writer plan, reclaim plan, recovery sequence, canonical image, and
lease identity are byte-sealed in protected executor storage. Independent
SHA-256 digests of both each working byte range and its duplicate are held in
the callback control snapshot and recomputed after every callback-bearing
boundary. Thus changing both arena copies identically is also detected before
any later program or erase.

Only after that zero-write phase does the transaction atomically take the
one-shot memory-clear completion grant into initially-zero protected storage.
The complete take, including its protection callback, is bracketed by the same
control and byte seals before the returned grant becomes live.
The grant's observed Control byte must equal the canonical store byte. A failed
take or mismatch cannot program or erase media. After a successful take, any
recovery, media, seal, or readback error permanently fails the variable
executor closed and cannot report success.

The sealed recovery plan is executed under the same lease. The writer plan is
then rebuilt from the recovered snapshot and required to match the preflight
plan and bytes exactly. The existing durable direct or FTW reclaim primitive
replaces Control with `old_value & ~1`, retaining all upper bits. Reclaim-image
bytes are checked against the sealed canonical source before each callback-
bearing media operation. Completion requires a synchronized full-store reread,
a clean FTW state, and an exact Control identity, attributes, size, and expected
value before the lease ends.

The entry point has no caller-provided pointer or descriptor. All retained
state is fixed, allocation-free protected executor storage, so there is no
external request/output alias surface.
