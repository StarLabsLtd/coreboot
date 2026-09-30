# Protected variable policy owner

The authenticated-variable executor owns one bounded policy ledger in SMRAM.
Registration and locking claim the existing executor transaction gate. SET checks
the same sealed ledger after recovery and before admitting either an ordinary or
authenticated write. The input entry is the EDK2 26.09 packed public layout, not a
new coreboot structure: 44-byte header and 18-byte lock-on-state extension.

Policies are selected by GUID, then exact name, hexadecimal `#` wildcard count,
then namespace. Duplicate literal policy keys are rejected. LOCK_ON_CREATE and
LOCK_ON_VAR_STATE read the same persistent/synthetic variable view used by GET,
including volatile mode variables. Registration stops at ReadyToBoot, runtime or
explicit interface lock. Repeated interface locking returns WRITE_PROTECTED.
There is no policy-disable operation.

Size constraints apply to incoming logical data. Auth2 envelopes are parsed to
locate that data before checking size; APPEND does not accumulate the old stored
value for this check. The store writer separately owns accumulated record-size
limits. Deletion skips size/attribute checks but never bypasses locking.

The two public C methods are protected owner calls, not shared-memory services.
No dispatcher, runtime protocol or platform selector is installed by this slice.
A future dispatcher must validate the caller, copy the complete bounded policy
entry into a distinct protected buffer disjoint from the executor arena and its
ledger, and call these methods under the existing authority. It must route both
ordinary and authenticated SET paths through this same owner. Advertising a
payload-local VariablePolicy protocol before those service operations exist would
not provide protected enforcement and remains prohibited.

The immutable ledger mirror participates in all existing executor consistency
checks. Specialized protected FMP/mode transformations remain trusted owner
operations; they are not new unmediated public SET entry points.

Host fixtures use external byte offsets and cover ordinary/authenticated denial
before programming, malformed framing, duplicate registration, logical Auth2
size, lifecycle closure, policy specificity, LOCK_ON_CREATE, volatile state
locking and APPEND size. These are host tests, not production-composition, QEMU
or hardware activation evidence.
