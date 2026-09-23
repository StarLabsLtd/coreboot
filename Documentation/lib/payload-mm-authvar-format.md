# Payload-MM authenticated-variable formats

The dormant authenticated-variable format parser is an allocation-free,
byte-wise SMM helper. It decodes unaligned external serializations into
bounded immutable spans; it is not an endpoint, authority, cryptographic
verifier or trust policy. No UEFI structure is cast over caller memory and no
input-backed pointer may be retained by a future protected policy provider.

## Auth2 envelope

`payload_mm_authvar_auth2_parse()` implements the bounded outer
`EFI_VARIABLE_AUTHENTICATION_2` shape used by EDK2 26.09. It requires the
40-byte timestamp and certificate prefix, a `dwLength` of at least 24 bytes,
and a certificate that fits entirely within the supplied buffer. It returns
the copied timestamp, certificate metadata, and bounded PKCS7 and payload
spans. Empty PKCS7 and payload spans are structurally valid; a later strict
CMS parser and variable policy decide whether they are meaningful.

EDK2 does not require a particular `WIN_CERTIFICATE` revision on this path, so
the parser records but does not reject it. Signed metadata validation exactly
matches EDK2's noncryptographic checks: `WIN_CERT_TYPE_EFI_GUID`, the PKCS7
certificate GUID and zero reserved EFI_TIME fields. It does not silently add
calendar policy.

The separate `payload_mm_authvar_timestamp_store_valid()` helper accepts the
all-zero initialization sentinel or the real calendar accepted by the native
store scanner. A future authority must apply both checks and return
`SECURITY_VIOLATION` for an invalid calendar. This is an explicit
malformed-input tightening over EDK2, necessary because accepting such an
update would create a store the next native scan rejects. Digest selection,
strict CMS decoding, replay ordering, append behavior, SetupMode, physical
presence and trust-root selection remain later policy stages.

The EDK2 26.09 oracle is commit
`aab7b589fc59b7e2b8fb7eb79519bf1a5e5a5272`: outer bounds are in
`MdeModulePkg/Universal/Variable/RuntimeDxe/Variable.c`, while signed metadata
checks are in `SecurityPkg/Library/AuthVariableLib/AuthService.c`.

## EFI signature lists

The cursor accepts an empty aggregate and otherwise consumes concatenated
`EFI_SIGNATURE_LIST` objects exactly. Each list must contain its 28-byte
header, fit in the remaining span, describe a bounded optional header, use a
signature stride large enough for the mandatory 16-byte owner GUID, and divide
the signature payload exactly. `DONE` means exact exhaustion; trailing bytes
are `MALFORMED`. Zero-entry lists are accepted, matching EDK2 26.09. A
malformed list does not advance the cursor, and every failed output is zeroed.

This generic walker intentionally does not decide which signature GUIDs or
entry sizes PK, KEK, db, dbx or dbt permit. It also does not parse X.509. Those
variable-specific and cryptographic rules belong to the later native policy
and crypto slices. Unlike EDK2's older format walker, this helper guarantees
forward progress, checked header arithmetic, a nonzero owner-bearing stride,
exact aggregate consumption and safe access to every entry.
