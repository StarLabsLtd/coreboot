## SPDX-License-Identifier: GPL-2.0-only

# Payload-MM private authenticated-variable signer trust

`PAYLOAD_MM_AUTHVAR_PRIVATE_TRUST` is a dormant SMM-only trust mechanic for
ordinary time-authenticated variables. It combines detached-CMS signature
verification with EDK2 26.09 private signer continuity in one protected call;
it never accepts a reusable caller-provided signer view.

The helper accepts only the canonical five signed authority spans and resolves
the target and literal persistent `certdb` from the same scanner-validated
protected store index. Signed attributes must include non-volatile and
time-authenticated access and remain within the admitted persistent profile.
Runtime access requires boot-service access. The private route and existing
target attributes must agree with the same index and signed request.

For a new target with a non-empty payload, the complete persistent `certdb`
stream must be present, canonically attributed, valid and contain no entry for
the key. Success publishes the incoming digest algorithm's signer binding for
a later atomic database composition. A new empty delete or append authenticates
CMS but neither locates certdb nor requires a usable signer CommonName,
matching EDK2 26.09. For an
existing target, the complete stream must contain exactly one entry and its
opaque bytes must match either the native CommonName plus signer
`TBSCertificate` digest or the exact legacy one-signer `EFI_CERT_STACK`.
Route/database disagreement and malformed protected state fail closed.

CMS signature integrity is checked before certdb continuity. This preserves
the accepted result while deliberately avoiding EDK2's unauthenticated
binding-first work. Reserved-key rejection and the remaining request policy
belong to the preceding preflight/route stages; detailed UEFI failure mapping
belongs to the later service provider, which must normalize private
authentication failures to `EFI_SECURITY_VIOLATION`. Candidate-space admission
and atomic target-plus-certdb composition remain later transaction seams.

The current service profile admits only non-volatile Auth2 variables, so this
slice deliberately consumes persistent `certdb`; volatile `certdbv` remains a
future profile extension. Inputs are sealed across the operation and the
crypto arena is idle and wiped before an authority or new binding is
published. The result is only a synchronous policy fact: no certdb mutation,
variable authorization, candidate construction, media access, service provider
or endpoint is present.
