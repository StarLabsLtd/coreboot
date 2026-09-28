# Authenticated-variable presence lifecycle installation evidence

`PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_EVIDENCE` is a hidden,
default-off ramstage prerequisite for a future authenticated-variable presence
platform composition. It proves only that four mandatory lifecycle sources
installed their registrations:

* pre-external-image;
* payload failure or return;
* warm reset; and
* S3 resume.

The pre-external-image source is valid only when a future provider is bound to
the exact artifact and provenance of the externally launched image. Internal
firmware-driver `LoadImage` or `StartImage` events do not satisfy this source;
in particular, the currently audited CDK2 PR 366 internal-driver path is not
the required boundary. This slice supplies no callsite or selected provider.

Each source owns one named static slot and one exact registration symbol. There
is no source enum, generic public issuer, caller-provided array, callback,
context, endpoint, generation, command, port or transport. The composer names
all four private providers directly. It first seals all four issued slots, then
consumes all four. Unexpected, duplicate, concurrent, corrupted or missing
registration is terminal. Consumed evidence is explicitly scrubbed, and the
completion query revalidates the redundant composition word and all four
source-owned consumed slots.

This evidence says that the four sources were installed and composed. It does
not say that any boundary executed, that protected authority closed, or that a
reset or resume occurred. It must never be used as the producer's
`lifecycle_sealed` callback and cannot establish
`LB_AUTHVAR_PRESENCE_LIFECYCLE_SEALED`. Later linear slices must add the actual
boundary callsites and protected completion acknowledgements before a sealed
arbiter can make that claim.

This slice installs no hook, callsite, runtime closer, lifecycle arbiter,
selector, route, handler, endpoint, transport or platform selection.
