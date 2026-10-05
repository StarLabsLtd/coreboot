Coreboot PR391 final-ROM CDK2 admission — bounded HOST gates

Signed source 75899edb629a08a101e0c65a5927f396c56c690a, parent
3e2609c8938a06c6bb782474c7a01209745a4319. Three changed paths are bound
by source.sha256; source-head.txt records the signed head. Literal public
execution originals: /home/sean/current693-fwui-qemu.PMINOC/
coreboot-final-rom-public-root.S51xLAp2/public.log, .status and .time.

Actual public command uses CDK2_TEST_SOURCE_REPO=/home/sean/Documents/cdk2
with make -C /home/sean/Documents/.coreboot-worktrees/
cdk2-final-rom-admission-after390 UPDATED_SUBMODULES=1 NOCOMPILE=1
test-cdk2-final-rom-admission. Raw zero: eleven tests, no skips.

The existing PAYLOAD_CDK2 glue validates its nested config through the
public config path, then builds the existing HOST raw scanner with readiness
enabled. It invokes the existing seven-argument strict-direct CBFS validator
only after files_added, preserving coreboot as the sole package owner. No
nested gitlink change or vendor initialization is required by this patch.

Eight HOST wiring models check parallel phase ordering, exact config/scanner/
validator arguments, config/scanner/validator failure propagation, non-CDK2
no-op and a missing-order-dependency opposition. Three additional tests use
five real pinned CDK2 9291e98a Git blobs in isolated temporary source trees:
actual config/header validation and HOST scanner compilation, refusal of the
unready absolute artifact goal, and malformed duplicate config refusal.
Those tests do not build a complete payload or package/boot an actual ROM.

Peer review caught the initial hook's unready artifact call despite its
seven fake boundary tests passing. The corrected hook adds real pinned-Make
checks and validates config first. An earlier public target ending in -test
was incorrectly routed to coreboot's unit-test frontdoor and reported no
rule; it was renamed, not counted as a pass. Existing original test records
and diagnostic history remain, without rewriting earlier failures.

This packet is bounded HOST wiring/pinned-Make evidence, not a final ROM's
actual admission, current nested-source bootstrap, latest full firmware
build, QEMU or hardware validation. Existing validator hostile suites and
the genuine native-menu producer/guest acceptance remain separate gates.
