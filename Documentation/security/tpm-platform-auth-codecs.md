# TPM2 platform authorization codecs

`TPM2_PLATFORM_AUTH_CODECS` is a default-off, dormant set of fixed TPM2 wire
codecs over the exclusive pre-OS lifecycle transport. No production platform
selects or invokes it. A Q35-only test option compile-checks the object in the
ramstage archive without adding a runtime reference; final-image dead stripping
is therefore expected at this layer.

The admitted commands are deliberately narrow:

- `NV_Write` uses platform password authorization, writes exactly one 40-byte
  value, and fixes the offset at zero.
- `NV_WriteLock` uses platform password authorization for one exact NV index.
- `HierarchyControl` closes `phEnable`; no other hierarchy or state is
  accepted.
- `GetCapability` requests exactly one `TPM_PT_STARTUP_CLEAR` property and
  accepts it only when the response contains that exact property and count,
  `moreData` is a valid zero-or-one TPM boolean, `phEnable` is clear, and
  `phEnableNV` remains set.

Success, delivered TPM errors, malformed replies and transport loss remain
distinguishable. A well-formed TPM error preserves its response code and is
reported as delivered; malformed replies and transport failures clear the
result. The lifecycle owner still controls acquisition, serialization and
terminal transport failure.

The authorized mutating commands deliberately encode `TPM_RS_PW` with an empty
nonce and empty HMAC. They therefore assume `platformAuth` is empty at the
instant of execution. They do not accept, derive, store or transmit a non-empty
platform authorization value. A platform that provisions `platformAuth` must
not use these codecs; a future authenticated-session design would need its own
reviewed secret boundary. This narrow assumption does not protect the commands
from already-compromised late firmware, which remains inside the weaker TCB.

A syntactically valid TPM error is trustworthy enough to report as delivered
and leaves the lifecycle owned. A malformed success or error reply is
untrustworthy: the codec clears its result and permanently fails the lifecycle,
because the platform hierarchy may still be exposed and no safe retry can be
inferred. Transport loss is terminal for the same reason.

This slice provides encoding and verification only. It adds no journal ABI,
resume path, production board selection, capsule composition or platform
state machine. Executing these commands from late mutable firmware would place
that firmware in the authorization TCB. Closing `phEnable` here therefore does
not claim the stronger protection of an earlier hardware-enforced root of
trust.

`make test-tpm2-platform-auth` runs the exact transcript and hostile-response
matrix directly. The normal `test-basic` CI target includes that target.
