# Payload-MM authenticated-variable authority policy

`PAYLOAD_MM_AUTHVAR_AUTHORITY` is a dormant SMM-only policy library. It does not
install the executor policy provider, publish a service, or touch storage. It
exists to keep authorization separate from the eventual atomic commit.

The input is one immutable protected request and scanner-produced store index.
The canonical scanner invariants are revalidated before any indexed byte is
used. Protected request, store, entry, workspace, and local verifier descriptor
state is hashed or copied across the synchronous verifier call; any change
invalidates the decision.
The authority parses Auth2, rejects the all-zero timestamp, enforces strict
non-append replay ordering, validates Secure Boot signature databases, applies
the route policy, and calls a synchronous protected verifier. The verifier sees
the exact five detached-CMS content spans used by EDK2 26.09: UTF-16LE name
without its final NUL, vendor GUID, raw little-endian attributes including
APPEND, raw EFI_TIME, and new payload.

APPEND deliberately skips replay rejection. Existing records retain the later
of their old and incoming timestamps. KEK and image-security databases filter
complete duplicate `EFI_SIGNATURE_DATA` entries exactly as EDK2 does. Native
hardening rejects PK APPEND and zero timestamps. Empty APPEND to an absent
target succeeds as a no-op and emits no derived-state intent. Existing APPEND
produces a complete canonical replacement value, removes APPEND from the final
attributes, and retains the later timestamp. Setup/custom bypass suppresses
only trust verification; envelope metadata, calendar, replay and payload
format are still enforced. On signed paths, authority acceptance precedes
payload-format validation, matching the EDK2 26.09 decision order.

Success returns an explicit mutation, no-op, or authenticated-not-found outcome.
The latter maps to `EFI_NOT_FOUND` only in the future service composer; it is
not a signature rejection. A mutation carries typed intents for SetupMode,
SecureBoot, VendorKeys, or a private signer binding. Those are not permission
to commit the target alone. A future composer must turn the mutation and every
intent into one recoverable multi-variable transaction. Until then the library
must remain unselected by production boards.
