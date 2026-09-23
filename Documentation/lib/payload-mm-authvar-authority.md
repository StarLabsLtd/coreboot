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

`PAYLOAD_MM_AUTHVAR_BUNDLE_PLAN` is the next dormant, pure stage. It consumes a
final authority decision, a freshly validated canonical store index, and
trusted boot/runtime mode facts. `NOOP` and `NOT_FOUND` produce no mutation or
mode effect. A mutation produces a deterministic role order: target first,
optional `SecureBootEnable` second, and optional `VendorKeysNv` last. The only
persistent roles are those three; `SetupMode`, `SecureBoot`, and `VendorKeys`
exist solely in one packed next-volatile-mode projection.

Pre-runtime PK enrollment writes `SecureBootEnable=1`; PK deletion removes it
when present. At runtime the EDK2 26.09 rule changes only the projected
`SetupMode`, retaining `SecureBoot` and `SecureBootEnable`. A required
non-runtime `VendorKeysNv` update rejects the complete runtime operation. The
planner requires the canonical `VendorKeysNv` record outside bootstrap, uses
its exact NV+BS+TIME_AUTH attributes and zero timestamp sentinel, and never
rewrites an already-modified value. It rejects every internal/derived variable
as a target, private certdb binding intents, aliases, malformed ranges, and
inconsistent target or mode facts. It still performs no media operation,
candidate-image construction, provider selection, or endpoint publication.

`PAYLOAD_MM_AUTHVAR_CANDIDATE` is the dormant pure image-construction stage.
It independently rescans the complete source, compares the supplied index,
copies the exact store header, compacts surviving ADDED records before promoted
transition records, then emits fixed roles in target/enable/vendor order. It
rescans the complete replacement image, checks every surviving and mutated key,
requires exact internal-variable representations, and publishes full-store
source and candidate SHA-256 digests with a nonzero generation/token binding.
No media is read or written by this stage.

The write policy is immutable trusted platform policy held in protected SMRAM,
not request or media data. The coordinator binds it to the active transaction;
the candidate result carries an exact policy copy so the later executor seal
can prove which limits admitted the image.

The binding also carries the trusted source volatile-mode projection and the
runtime phase. SetupMode and VendorKeys are independently checked against PK
and VendorKeysNv. Before runtime, SecureBoot is also checked against
SecureBootEnable. At runtime SecureBoot has no authoritative persistent source:
EDK2 deliberately preserves its current volatile value while a PK transition
does not update SecureBootEnable. The protected transaction coordinator must
therefore bind that volatile source fact to the generation/token; the candidate
builder requires an absent derived role to preserve it exactly. Persistent
SecureBootEnable and VendorKeysNv roles are rejected at runtime. The resulting
projection is checked against final PK/VendorKeysNv state and every explicit
SecureBootEnable transition.
