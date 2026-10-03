# Standard CapsuleReport source checkpoint after 599

Ready source: https://github.com/StarLabsLtd/cdk2/pull/599.
Signed head `8276b68eea49ee0620e4fdcdd49bb2386e0bc7cf`, after
`477c30e7100f7ddf489a5c2d84979b0b4d89eb1c`, based on ready 598
`15c3bc1109ab39f06d1298895fdd95ae0dec6759`.

The archive contains exactly the fourteen changed source/test/recipe paths.
No binary, firmware, media, private key or variable-store contents are copied.
The standard encoder writes a 72-byte Capsule#### report (attributes 7), then
an exact 22-byte UTF-16 CapsuleLast without a terminator (attributes 7).
CapsuleMax is an exact 22-byte volatile value (attributes 6), default FFFF,
with the actual VariablePolicy LOCK_NOW route. Normal/flash Last behavior
uses the existing validated handoff boot mode. PayloadIndex is the actual
zero-based Runtime loop index, not a fixed assumption.

Reference: local EDK2 release_26.09 branch at
`aab7b589fc59b7e2b8fb7eb79519bf1a5e5a5272`; no 26.09_dave ref was found.

## Actual receipts

- Author corrected normal Core session 60133 returned 0: GNU real 69.77,
  user 91.86, system 20.70 seconds. All fourteen source and two configuration
  fingerprints checked unchanged after the build. External immutable ELF:
  `/home/sean/capsule-standard-report-header-core.eEzkO9/native/cdk2-coreboot-image.elf`,
  SHA256 `ed0e1cb0b413f2eb32ff1f6665cde9588ef3442d9ec7318da44be6557ed056a7`.
- Author final original-header causal session 89706 returned 0: real 45.38,
  user 42.78, system 2.44 seconds. Independent final header session 51181
  returned 0: real 45.03 seconds (full GNU values in copied raw time file).
  Both cover O0/O2 encoder/Core/provider/Runtime positives, thirteen Core
  lifecycle scenarios and six exact O2 guard-discard causes. Each cause is
  an uninstrumented intended libc assertion 134, with no sanitizer report
  and full inverse translation-unit comparison. Core heap/stack ASan and
  UBSan positives disable ASan global registration; provider/Runtime and
  report encoder positives retain their strict sanitizer qualification.
- Independent earlier fourteen-path session 84655 returned 0 (40.42 seconds)
  before the three-path original-header correction. Its five-cause receipt
  is preserved separately and is not relabeled as the final six-cause run.
- Author genuine installed protected/legacy policy session 17339 returned 0
  (133.30 seconds). Independent final-parent policy session 77322 returned 0
  (141.07 seconds). Real protected router/store/producer services publish
  and read the report and Last; trigger/HOB and clock callbacks are HOST
  models. The five explicit newly joined report/ABI input hashes and final
  fourteen-path held manifest are copied. These helper/policy bodies did
  not change in the header-only followup.
- Author genuine disabled-profile session 19964 returned 0 (7.31 seconds):
  supported strict compatibility and legacy entry targets linked the real
  P0 Core without the conditionally selected report object. The compatibility
  oracle's documented SKIP is not represented as a strict-profile pass.

The original snapshots precede apply/CheckImage. Exact header identity is
rechecked before certification and after CLOSE, report callbacks and source
retirement. Copied capsule metadata is used by the writer. Callback mutations,
report/readback/policy failures and unproved/mixed/SET outcomes remain fatal.
CHECK leaves FmpState unchanged; report timestamp is not authority.

## Preserved failures and limits

The first-final log contains the real assertion 134 from a HOST padding
mutator accidentally toggled twice. The third-final log contains the real
assertion 134 from incorrectly expecting a second failed CHECK's payload
index after a prior SET; that aggregate must be ineligible. Both test-only
expectations were corrected without weakening production gates.
Other prior harness failures remain at their original external receipt paths:
wrong script argument/copy failure; missing modeled image GUID; truncated
legacy policy view static assertion; unsupported selected invocation of a
legacy fixture; and an inappropriate P0 native-coreboot-image invocation.
They are not successful gates or native failures.

The earlier 477c/Core22f native report continuation is prior-source evidence,
not final original-header native proof. This packet contains no native run.
Final corrected-header warm report and cold persistence/lock proof remain
separate. No local below-floor recovery, SET failure history, durable atomic
report transaction, hardware, Linux boot or power-loss guarantee is claimed.
An earlier report write may remain if a later report/readback or callback
guard fails; such failure does not authorize retirement or continuation.

Relative fingerprints must be checked from their actual original worktree,
not from this packet. The independent header manifest's first wrong-worktree
postcheck failed; the corrected check in the report worktree passed. The
selected-source-held manifest was captured while that gate was active, not
fabricated as a pre-launch manifest. Packet SHA256SUMS bind copied bytes.
