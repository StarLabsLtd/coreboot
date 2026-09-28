# Authenticated-variable presence lifecycle-close endpoint

This dormant facility defines the fixed coreboot-table and mailbox ABI for
requesting an exact-generation lifecycle close from an already installed,
protected SMM owner.  The table record is 64 bytes with tag `0x0057`; its ten
flags and three-source mask are exact, not extensible acceptance masks.

The public sources are pre-external-image, payload-failure-or-return and the
ordered CLOSED reproof. Warm-reset and S3 closure remain firmware-internal and
are deliberately not exposed to an untrusted payload. The 64-byte mailbox is
8-byte aligned. A request starts with pending status and completion; the
response may change only the fixed status and final completion word. Writers
must store completion last and readers must acquire it first.
Response validation requires a separate immutable saved request snapshot;
exact or partial request/response aliasing is rejected.

The backing owner requests a distinct page-rounded 4 KiB `BM_MEM_RESERVED`
range below 4 GiB. A future protected provider must privately take that page,
install and prove its route, and return a sealed ready receipt. Table
publication consumes that exact one-use receipt. Reservation contents, a
plausible endpoint, or the mere existence of backing can never establish
readiness.

The future provider must bind the receipt generation to the already sealed
authenticated-variable presence endpoint generation and its exact protected
lifecycle-close owner. This slice cannot mint that evidence independently;
its backing owner only checks and transfers the separately supplied receipt.

Both provider capability and endpoint are default-off. This slice supplies no
provider, route, sender, selector, board enablement or live callsite, and it
does not claim `LB_AUTHVAR_PRESENCE_LIFECYCLE_SEALED`.
