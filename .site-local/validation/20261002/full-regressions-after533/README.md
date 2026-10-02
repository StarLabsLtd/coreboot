# Reaped combined results through PR533/534

The full default `review-profile-check check native-stage` suite was reaped
with exit 0 at immutable clean CDK2
`dc2b0c60f233f3e306d5fd577db93260e8fe1bc2` (PR533).
Unit `cdk2-root-full-default-after533`, invocation
`883ebf81a5dd49b191c2565a4082e553`, runtime 22m24.042s,
CPU 45m1.115s, unit peak memory 1013.4M. The source remained at the same
clean HEAD after reap. This reused the actual prior default build directory.
The actual configuration/header SHA256 values remained
`8a70dc70dc9cb75849aa1bd9cc15fa472a22a1a9d131a011b38a290130cdba46` and
`48556c4c243ffc3e67c3f91293ed3c220e105edee946f739ede45ec40094167d`.
The stage ELF SHA256 remained
`5f152ef18202b254594524f41b217c91d8d0e29b71a59fc43577d19d3867e713`.
This is not later-source or protected/hardware sign-off.

The fresh root producer-bound selected VariableRuntime aggregate at signed
`61f382de12c1a595d2bbbee6c947e49fb5fca76a` (PR534) was reaped with
exit 0, including the filter/strict-ledger gates and genuine protected
counterparts. Unit `cdk2-root-variable-runtime-split-after533`, invocation
`58bff716b5ea49738f2990f2312055c3`, runtime 4m32.158s,
CPU 6m14.875s, unit peak 478.6M. P1 and attested-route header symbols were
actually 1, DEBUG was 0, and the isolated legacy fixture checked unchanged
caller configuration/header fingerprints. The generic legacy fixture is
not protected authentication authority.

The full protected suite at the same `61f382de12` was reaped with exit 2.
Unit `cdk2-root-full-protected-after534`, invocation
`9c886274421c458794845d3d8bcc777e`, runtime 15m12.412s,
CPU 31m5.908s, unit peak 576.1M. This also reused its actual prior build.
Two manual Core HOST recipes failed to link the real
`cdk2_direct_image_table_record` implementation:
`native-dxe-core-dispatch-policy-test` and
`native-linear-xhci-poll-boundary-test`. The bounded source closure fix is
pending. This is an actual retained failure, not a full-suite pass.

Only text receipts are archived; no complete images, variable media or private
signing material. Genuine capsule installation/restore, current DMA ownership,
final hardware and fresh reviewer sign-off remain separate release gates.
