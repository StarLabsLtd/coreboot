<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Authentication fixture provenance

The generated fixtures are the CDK2 SystemFmp authentication test corpus. Their
decoded hashes match CDK2's `system-fmp-native-authentication.tsv` evidence
ledger. They exercise the EDK2-compatible authenticated-image envelope,
detached CMS profile, partial-chain anchors, certificate-time and purpose
semantics, and strict rejection cases.

`real-cms.der`, `real-digest.bin` and `real-trust.xdr` are an oracle extracted
from the validated EDK2 capsule whose SHA-256 is
`621eb961c265f970ee9e11406a771bf07d3b029b828fa7d37186889da8eccc80`.
The retained 1,442-byte policy trust set has SHA-256
`d796d9a191fefdba7e730c462f0286bf743f3ff5b1a38281aaace1e21d7350de`.
The extracted CMS preserves its noncanonical CertificateSet order. Tests reject
duplicate members, normalize this bounded unsigned set before chain selection,
and prove identical signature/root results for both member orders.

`SHA256SUMS` pins every decoded fixture used by the test. The Base64 files are
generated binary test data; this notice and the verification results make no
license claim about embedded test certificates or keys beyond their fixture
use.
