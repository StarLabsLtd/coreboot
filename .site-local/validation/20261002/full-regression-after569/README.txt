Full default/protected regression at signed CDK2 PR569
====================================================

Source: c1e16e4c58aa2ddfdcf0b4f94feaab20dc844ef5 (signature G).
Both source worktrees remained clean at that head through completion. The
source archive was exported after the runs from that signed commit. It does
not include Git submodule contents or pretend to be a full dependency bundle.

Actual outer sessions reaped by root: default 15169 exit0; protected 15853
exit0. Each command ran review-profile-check, check, native-stage, then checked
the two generated config digests captured before its run. Both checks returned
OK. Root repeated those current digest checks when packaging: all four OK.
Raw named command and GNU time output are embedded at the end of each log.
Default aggregate: 49:45.77; protected aggregate: 51:09.25. These are whole
test invocations, not firmware boot times.

Protected used the actual quiet setup acceptance defconfig and the genuine
q35-protected-ui producer config copied here. Native-stage is a build gate;
these whole-suite results are HOST/build coverage, not a new full production
capsule boot, hardware test, or complete EFI-loader matrix. Modeled protocol
fixtures retain their existing modeled semantics. The newer normal production
capsule acceptance at PR573 is a separate receipt, not attributed to PR569.

The logs and config-before manifests are exact copies of the original named
run receipts. Configs/header bytes are packaged only after confirming their
original before hashes still match. No ROM, guest disk, private signing key,
token, or private certificate key is included. This checkpoint precedes
PR570--573 and does not claim final-tree regression or release sign-off.
