# Payload-MM authenticated-variable protected-store trust policy

`PAYLOAD_MM_AUTHVAR_TRUST_STORE` is a dormant SMM-only policy mechanic. It
consumes an immutable index produced by `payload_mm_authvar_store_scan()` in the
same protected transaction, a canonical user-mode routing plan, detached CMS
and the exact signed content spans. CMS verification and protected-store trust
matching occur in one call; no caller-provided signer view is accepted. It
publishes no token, provider, endpoint or mutation authority.

The call initializes its admitted authority output to `NONE`. Only an `OK`
result publishes the exact route entry which accepted the signature:
`CURRENT_PK` or `CURRENT_KEK`. Rejection and every format, resource or internal
failure retain `NONE`. Publication occurs after route and index descriptors are
confirmed unchanged and the crypto owner is idle and wiped. The authority is a
scalar result for the enclosing synchronous policy decision, not a reusable
capability.

PK and KEK updates require the current PK. db, dbx and dbt try the current PK
first and then each X.509 certificate in the current KEK, matching EDK2 26.09.
The PK path requires its X.509 certificate to be the exact CMS signer and to
appear exactly once in the CMS certificate set; PK authorization permits no
certificate chaining. KEK iteration
skips supported non-X.509 lists and admits at most 64 X.509 trust anchors, which
bounds certificate parsing and arena wiping in SMM. Selected signature-list
streams are validated completely against EDK2 26.09's 12 supported signature
types before use, so malformed or unknown protected state fails closed rather
than authorizing through an earlier entry. Selected PK and KEK records must
also carry the canonical non-volatile, boot-service, runtime and
time-authenticated-write attributes.

The native PK rule is deliberately stricter than EDK2's generic list-format
check: PK must contain exactly one X.509 entry. A malformed selected database
is terminal rather than skipped in favor of a later authority.

The input index is not an authorization token. This layer checks every indexed
entry against its physical record before lookup, but relies on the scanner and
protected transaction for completeness, replacement semantics and immutability.
Setup/custom bypasses, private certdb continuity, signed Auth2 span construction,
timestamp replay, payload semantics and atomic store mutation remain separate
work.
