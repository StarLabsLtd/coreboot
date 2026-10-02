Owned single-use helper cleanup: bounded WIP receipts

Signed source chain (all signatures verified G, published WIP, no ready PR):
  baseline 66966831811127781302929487f01efaed2dbec4
  ESRT a9f21b2d888cf1051f1cc37eff82fb24af493712
  variable model 9ce1d72e8c5a8ecdefed602d059cb3a108128ded
  durable state d4a24041216d1737656aa665cf4c5803c3fcd6cc

Exactly three production files change. ESRT reuses its existing identical
16-byte GUID comparator. Variable lookup replaces a single-use memcmp wrapper
with the same comparison at the same occupied/name-size/GUID/name position.
State execution replaces a single-use normalization wrapper with the identical
inclusive MIN/MAX ternary only in the writer-error branch. Public types, ABI,
callback orchestration, status/persistence ordering and diagnostics are unchanged.

Actual author session 40048 returned 0 for normal genuine-default named targets
native-esrt-test, native-variable-runtime-test and native-system-fmp-state-test.
Raw named.log and GNU named.time record elapsed 8.16, user 7.33, sys 1.11 seconds.
Existing ESRT duplicate/restrictive-descriptor tests, variable model/service/
backend/malformed-record tests and state O0/O2/O1 sanitizer tests passed. Existing
state cases include MIN-1, MIN, MAX, MAX+1 and persistence-refusal ordering.

Supplemental source-bound test source is retained in both receipt directories.
Author session 92250 and independent peer replay both returned 0. Each performs
four unique exact-one mutations at O0/O2: ESRT duplicate GUID comparison inversion,
variable GUID comparison inversion, and exclusion of the exact state MIN/MAX.
All eight strictly compiled mutants exit 134 at the intended fixture assertion;
the logs are checked for the intended expression and no sanitizer diagnostics.
Every inverse reconstruction is compared against the complete production source.
The positive GUID test flips each of the 16 GUID bytes and checks a name mismatch;
author and peer both executed it successfully at O0/O2 (tool receipts, not a
separately captured positive-run raw log). Peer elapsed time was not measured.
The scripts differ only in their artifact output path. No canonical source is
modified by the supplemental tests. Binaries are omitted; logs and full mutated/
inverse source copies are retained for independent reconstruction.

Project patch-style check returned 0 with 0 errors, 0 warnings and 68 lines checked.
This is not a whole-tree lint pass. Author/peer source-before manifests match the
signed candidate bodies, and the copied real HOST config headers are identical.
One author checksum command was initially run from the artifact directory and
failed to find relative source paths; the corrected source-directory check passed.
That read-only harness mistake remains in the tool transcript, not a forged log.

source/signed-candidate.tar contains the real signed includes, production bodies
and existing ESRT/state fixtures required by the causal script. To reconstruct,
extract it into a fresh source directory, copy the external GUID fixture and
script into a fresh proof directory, put inputs/author-config.h at that proof
directory's build/include/cdk2/config.h, and change only the script's root/proof
absolute paths. Then run sh causal-test.sh. The source-before relative manifest
is verified from the extracted source root. GUID positives compile with the same
actual header plus the extracted variable model. No policy macros are seeded.

These are bounded HOST semantic/style proofs on the reviewed WIP source, not
native MM admission, firmware update, hardware or final-parent regression proof.
Final linear-parent focused gates remain required before publication. No images,
private keys, secrets, guest media or native authority claims are included.
