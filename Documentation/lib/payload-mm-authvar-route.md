# Payload-MM authenticated-variable authorization routing

`PAYLOAD_MM_AUTHVAR_ROUTE` is a dormant, allocation-free decision table. It
classifies a canonical protected request snapshot and describes which authority
a later native policy must prove. It does not parse Auth2, verify CMS or X.509,
read the variable store, validate replay or payload semantics, authorize a
mutation, install a provider, or publish an endpoint.

Names are exact UTF-16LE byte strings without a terminating NUL. `PK` and `KEK`
are recognized only under the EFI global-variable GUID; `db`, `dbx` and `dbt`
are recognized only under the image-security-database GUID. Every near miss is
a private variable. Secure Boot targets require the nonvolatile and time-based
authenticated-write attributes and reject counter-based authentication.

The standard user-mode routes match EDK2 26.09: PK and KEK updates require the
current PK, while db-family updates try the current PK and then current KEK in
that order. Setup mode emits a signature bypass except when self-signed PK
enrollment is required, in which case the new PK payload certificate is the
authority. Custom mode grants a bypass only when the protected snapshot says a
platform-owned physical-presence source is asserted. A request or payload must
never supply that fact. `SecureBoot` is intentionally not an input: EDK2 bases
these authorization decisions on `SetupMode`, even when image verification is
disabled.

Private time-authenticated variables route first creation to a new signer and
subsequent updates to certdb continuity. Counter-authenticated variables are
unsupported. An existing authenticated variable cannot be rewritten unsigned;
ordinary variables route to no authentication authority. Certdb parsing and
multi-variable atomicity remain separate work.

The plan also records post-success intent rather than performing side effects:
PK enrollment can enter user mode, authenticated PK deletion can enter setup
mode, and a user/custom physical-presence bypass can mark vendor keys modified.
Deletion is defined as an empty non-append payload for an existing target; this
avoids EDK2's inconsistent empty-append bypass behavior. Signature-list payload
validation, duplicate filtering and all mode-variable writes remain later
composition.

A valid disjoint output is zeroed on failure and populated only on success; an
invalid or aliasing output is untouched. The output must not overlap the
request or name, names are bounded to the native store's 4096-byte limit, and
all pointer arithmetic is checked. This makes the plan safe to consume inside
a future single protected executor transaction, but the plan itself is not an
authorization token.
