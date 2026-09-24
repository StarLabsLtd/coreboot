## SPDX-License-Identifier: GPL-2.0-only

# Payload-MM private authenticated-variable signer binding

`PAYLOAD_MM_AUTHVAR_PRIVATE_BINDING` builds a dormant pure implementation of
the EDK2 26.09 certdb signer-binding rule. For the supported exactly-one-signer
CMS profile, EDK2's `Pkcs7GetSigners()` returns the SignerInfo certificate as
both the first stack entry and the misleadingly named top-level certificate.
The native binding is therefore the incoming CMS digest algorithm over the
first subject CommonName converted to UTF-8 and the exact DER
`TBSCertificate` element of that same signer certificate. It is not a digest
of an embedded root or intermediate.

The CommonName conversion follows EDK2's OpenSSL path for UTF8String,
PrintableString, IA5String, T61String, BMPString and UniversalString. EDK2
copies at most 127 converted bytes into its fixed buffer, even when the cut
splits a UTF-8 sequence, then hashes through the first zero byte without the
terminator. Later CommonName attributes are ignored. Malformed encodings and a
missing first CommonName fail closed. EDK2/OpenSSL accepts additional ASN.1
string tags, but the deliberately narrower CMS/X.509 profile rejects them
before publishing a verified signer view; BIT STRING is likewise rejected.

An existing binding whose size equals the incoming SHA-256, SHA-384 or SHA-512
digest size is compared only with the derived digest. Every other size is
compared only with EDK2's legacy single-signer `EFI_CERT_STACK` serialization:
one byte containing `1`, a little-endian 32-bit signer DER size and the exact
signer DER bytes. The formats are never tried as fallbacks for one another.

The helper uses the existing bounded protected crypto arena and publishes a
new digest only after parsing, hashing and owner cleanup succeed. All CMS views
must be exact, disjoint subranges of one immutable protected input. It performs
no trust decision, certdb lookup or update, variable authorization, media
operation, service dispatch or endpoint publication.
