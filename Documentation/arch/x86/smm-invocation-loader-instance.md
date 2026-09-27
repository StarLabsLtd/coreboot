# Protected SMM invocation loader instance

`SMM_INVOCATION_LOADER_INSTANCE` is a dormant, default-off primitive. It
release-publishes a pointer-free protected descriptor containing an exact
lifecycle and a caller-supplied 128-bit correlation nonce. It installs no
nonce provider, platform selector, command, reset path, public record or SMM
consumer.

The nonce is two explicit 64-bit words. Either word may be zero, but both may
not be zero. It is tested only for exact equality. A future provider must
supply a newly sampled value for every firmware/SMM loader instance, but this
primitive does not prove uniqueness, ordering, monotonicity or rollback
resistance. Matching protected state plus a repeated nonce remains a replay
problem for the eventual composition.

`SMM_INVOCATION_LOADER_NON_S3_LOAD` covers every loader run other than an S3
reload, including cold boot, warm/global reset, S4 and S5. An S3 handler reload
requires `SMM_INVOCATION_LOADER_S3_RELOAD` and a newly sampled nonce. S0ix does
not reload firmware or SMM and therefore retains the current loader instance.

Publication snapshots and validates the complete seed before atomically
claiming an exact-zero `EMPTY` destination. The owner rechecks both source and
destination before release-publishing `READY`. Failed owners scrub before
returning to `EMPTY`; failed contenders never alter the owner's object.
Readers acquire `READY`, copy the complete descriptor and recheck the source.

The public scrub and loader-result helpers are loader-quiescent operations.
They must not race publication or a reader. A `READY` payload is immutable.
The later loader composition must publish topology first, publish this
descriptor second, snapshot and recheck both before provisioning invocation
evidence, and unwind in reverse order on failure.

No current board selects this option in a checked-in configuration. StarBook
MTL has a separate default-off provider prerequisite described in
`Documentation/mainboard/starlabs/starbook-mtl-loader-instance.md`. It closes
the retained-classification, protected-range and S3 stale-record gaps and fans
out one protected random sample. It does not yet call this generic primitive
from the SMM loader. ADL and GLK remain compile-only profiles. This primitive
does not repeat VT-d, bus-master, protected-range or reset work performed
elsewhere.
