## SPDX-License-Identifier: GPL-2.0-only

# Payload-MM authenticated-variable authority provider

`PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER` is the dormant concrete verifier for
the authenticated-variable coordinator. It admits only a canonical protected
authority request, independently seals its outer and payload descriptors, and
dispatches exact PK/KEK trust routes to the protected trust store or an exact
private route to private signer trust. It performs no media operation and
installs or publishes no endpoint.

The selected profile supports persistent private Auth2 variables only. A
volatile private route is authenticated neither through persistent `certdb`
nor through an absent `certdbv`: after complete descriptor, alias and protected
index admission it is rejected without invoking either trust helper. Volatile
`certdbv` remains a later transaction feature.

Private CMS, signer-continuity and helper resource/algorithm failures are
normalized to authentication rejection, which the coordinator maps to
`EFI_SECURITY_VIOLATION`. This matches EDK2 26.09's boolean private PKCS7 path.
Workspace exhaustion before the provider, and generic whole-store candidate
capacity after it, retain `EFI_OUT_OF_RESOURCES`. The bundle's private
`certdb` duplicate, malformed or logical-capacity failures are already
authentication rejection. Invalid trusted descriptors remain caller-contract
errors; busy, changed and internal failures remain invariant/device errors.

Success is accepted only when the helper returns the exact route authority and
a canonical inline private binding: 32, 48 or 64 bytes for a new non-empty
private value, and no binding for an empty new value or an existing signer.
Failure publishes a zero result. Unsafe or aliasing output is untouched.

Self-signed PK enrollment uses the separate `NEW_PAYLOAD_CERT` authority. It
is deliberately unsupported here, and Kconfig prevents selecting this provider
with `PAYLOAD_MM_AUTHVAR_REQUIRE_SELF_SIGNED_PK` until that trust mechanism is
implemented. The normal Q35 executor proof selects the provider and therefore
compiles the exact dormant production source without publishing it.
