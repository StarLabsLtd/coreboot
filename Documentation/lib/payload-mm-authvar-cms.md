# Payload-MM authenticated-variable CMS mechanics

`PAYLOAD_MM_AUTHVAR_CMS_VERIFY` builds a dormant, SMM-safe detached-CMS
mechanism. It verifies that one embedded certificate signed caller-supplied
immutable content, then returns input-backed views of the signer and bounded
certificate set. It does not decide whether that signer is trusted, whether a
variable update is authorized, or whether any protected endpoint exists.

The mechanism accepts the EDK2 26.09 authenticated-variable digest suite:
SHA-256, SHA-384 or SHA-512 with RSA PKCS#1 v1.5. It deliberately retains the
narrow coreboot CMS profile: canonical DER, exactly one signer, detached
content, signed attributes containing exactly one content-type and one message
digest, bounded embedded certificates, and RSA keys from 2048 through 8192
bits. Both bare SignedData and its ContentInfo wrapper are accepted. Unknown,
duplicate or unsigned attributes, attached content, BER encodings, multiple
signers and other signature schemes are rejected. These are intentional
fail-closed restrictions compared with EDK2's general OpenSSL consumer.

The caller supplies up to five spans so a later authority can authenticate the
EDK2 byte sequence without allocating or concatenating it:

1. UTF-16LE variable name, excluding its terminator;
2. the 16-byte vendor GUID serialization;
3. the raw little-endian 32-bit attributes, including `APPEND_WRITE`;
4. the raw 16-byte `EFI_TIME` from the Auth2 envelope;
5. the variable payload.

This function verifies only the CMS signature over that content. A later
native authority must independently validate the Auth2 metadata and timestamp,
select PK, KEK, db or certdb trust according to the variable and platform
state, enforce replay and append rules, and copy any retained certificate data
into protected storage. The returned certificate spans alias the immutable CMS
input and expire with it; they are explicitly not authorization facts.

All variable-sized Mbed TLS state uses the protected owner's bounded arena.
Calls share the existing global busy latch, leave the output untouched on any
failure, wipe the complete arena on return, and publish the result only after
the protected transaction has ended successfully. Mutable owner and result
storage must be aligned and disjoint from every input descriptor and byte
range. SHA-384/512 code is selected only with this authenticated-variable
mechanism, so the existing capsule-only SHA-256 footprint and policy remain
unchanged.

`tests/lib/payload_mm_authvar_cms_test.sh` creates fresh detached RSA fixtures
with OpenSSL for all three digests, checks them with OpenSSL independently, and
runs the coreboot verifier at O0 and O2 under ASan and UBSan. It covers the
five-span layout, content and signature mutations, algorithm mismatch, input
bounds, busy contention, injected allocation failure, atomic output publication
and complete arena wiping.
