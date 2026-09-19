# Payload-MM authenticated-capsule verification core

`PAYLOAD_MM_CMS_VERIFY` is an unselected SMM-capable prerequisite. It does not
publish a coreboot table, register an SMI command, choose trust anchors or
authorize a flash operation. It includes an unselected protected SystemFmp
policy owner and capsule-broker authentication provider; no platform installs
that owner in this tree.

The parser accepts the retained EDK2 authenticated-image envelope and detached
CMS SignedData profile used by SystemFmp: SHA-256, RSA PKCS#1 v1.5, canonical
DER, one signer, bounded embedded certificates and an explicit XDR trust set.
Canonical validation covers the complete SignedData structure and certificate
bodies with a 512-object, 12-level bound. Signed attributes retain strict DER
SET OF ordering. CertificateSet ordering is the narrow compatibility exception:
deployed EDK2 emits its unsigned, implicitly tagged certificate members in
non-DER order. The parser accepts either input order, rejects duplicate member
encodings, and sorts the bounded set before signer and chain selection so order
cannot affect the result.
Certificate time and purpose are deliberately outside this primitive, matching
EDK2's capsule policy; the future protected provider owns those policy choices.
The content digest is always computed from the authenticated image bytes. A
digest supplied by a payload is never an authorization fact.

The envelope, strict CMS profile and oracle corpus are ported from CDK2's
tested SystemFmp authentication series, principally commits `2e3bb471aa`
(`system_fmp: own authentication envelope`) and `3097ed7863`
(`system_fmp: add native PKCS7 verifier`). The implementation here uses
coreboot types, ownership and build rules rather than retaining an EDK2 ABI.

Every variable-sized Mbed TLS object comes from the 512 KiB arena embedded in
`payload_mm_crypto_owner`. The owner must reside in protected memory. Calls are
serialized by a busy latch, bound by allocation/count/byte/depth limits, and
wipe the complete arena before returning. There is no fallback to a general
allocator. The selected Mbed TLS 3.6.6 closure is limited to the fifteen files
listed in `src/lib/Makefile.mk`; upstream `pkcs7.c` is not used because the
strict profile and canonical-DER checks live in the coreboot-owned parser.

Mbed TLS is pinned at tag `v3.6.6`, commit
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Its selected files are available
under `Apache-2.0 OR GPL-2.0-or-later`; this GPL-2.0-only program uses the latter
option. See `3rdparty/mbedtls/LICENSE` for provenance and license text.

`tests/lib/payload_mm_crypto/test.sh` runs the committed CDK2 corpus and the
extracted EDK2 CMS/root oracle. The corpus proves equivalent results for both
CertificateSet orders and rejection of duplicate certificates. Setting
`PAYLOAD_MM_REAL_CAPSULE` to the pinned 16,781,014-byte capsule additionally
verifies its hash, extracted CMS and content digest before passing the complete
authentication image through the native path.

`tests/lib/payload_mm_fmp_auth_policy_test.sh` adds hostile policy, state, MSS1,
dependency and ROM-layout cases at O0 and O2 and under ASan/UBSan. It also makes
a fresh RSA root and authenticated MSS1 capsule, verifies the detached CMS with
OpenSSL, then passes that same image and XDR root through the complete provider.
