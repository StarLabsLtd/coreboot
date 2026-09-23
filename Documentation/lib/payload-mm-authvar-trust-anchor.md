# Payload-MM authenticated-variable trust-anchor matcher

`PAYLOAD_MM_AUTHVAR_TRUST_ANCHOR` builds a dormant SMM-safe X.509 matcher for
authenticated variables. It checks a signer chain returned by the strict
detached-CMS verifier against exactly one caller-supplied DER certificate. A
later policy owner can invoke it in the route's EDK2 26.09 order: current PK,
then current KEK for `db`, `dbx` and `dbt`.

This generic matcher permits a valid chain to its anchor. EDK2's current-PK
branch additionally requires the CMS top-level certificate to be the current
PK and labels that path as no-chaining. The later policy owner must enforce
that exact PK identity before accepting this mechanic's result; matcher success
alone is deliberately insufficient.

The matcher does not read a variable store, parse an EFI signature list, choose
an authority, accept an index, authorize an update or mutate state. Its success
is not a reusable authorization token. A later protected owner must retain the
verified CMS result locally, extract each candidate anchor from a bounded
protected-store snapshot and combine this mechanic with routing, replay,
payload-format and commit decisions.

All certificate views must be nonempty, pairwise-disjoint exact subranges of
the immutable CMS buffer, and the signer must equal exactly one member. The
external anchor must occupy a disjoint protected-store buffer; it may contain
the same DER bytes as an embedded top-level certificate. The CMS limit remains
eight certificates. Chain verification separately permits those eight plus the
one external anchor, and retains the existing per-certificate and aggregate
byte limits. Owner, CMS-span, verified-view and anchor-span descriptors must be
pairwise disjoint and may not alias either byte buffer, so no descriptor can be
sourced from the untrusted CMS.

The caller's protected crypto owner provides the only variable-sized memory.
The matcher enters it once, returns through the common cleanup path and wipes
the complete arena on every attempted chain verification. Invalid boundaries
are rejected before entering the owner.
