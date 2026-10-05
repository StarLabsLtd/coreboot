Native admission efficiency — bounded HOST evidence, 2026-10-05

Signed source ba629cf33fb4fa99e33989ff557b45877c874ee1, PR702;
parent b5f6262973074ce79103ee8dfdc2b32d7594c6a8, PR701.
Final reviewed exact two-file delta SHA256:
25bd905f324db84c2fcc2e24ae6a1436bb9be5bae5f11014e5f15c8192c70cb7
Original ROOT records:
/home/sean/current693-fwui-qemu.PMINOC/admission-efficiency-host.x2kbguEa
Author's final bounded fixture execution:
/home/sean/current693-fwui-qemu.PMINOC/admission-author-host.sT4Iwd

Every flat original is copied literally. Final five selected *-final suite
statuses are 0: fresh admission14, normal admission11, execution5, inputs4,
controller51 = 85 tests. Stable16 lint status 0. Author final14-test status 0
is separate from Root's independent final execution. These are HOST models,
not sanitizer, actual guest or hardware acceptance.

Original early suites at the initial optimization passed separately. The
fresh-final and fresh-final-pathfix statuses remain 1: first the finite new
model mishandled string vendor paths; then its output ledger omitted the
real recipe MMD edge. Corrections normalize the test path and include the
actual recipe in the ledger; production guards/assertions were not weakened.

Native-closure diagnostic uses the actual immutable build700 inputs. Baseline
52.2843 seconds, candidate 28.4717 seconds, identical 35723 lexical paths.
Full build-receipts diagnostic explicitly composes candidate code with the
unchanged signed700 observer/helper; it is NOT signed candidate admission.
Both real config/loaded-Core/direct-pair/reference replays ran unmocked in
new output directories, not in immutable build700. Both descriptors compare
exactly to build700/fresh-build.json and ROM/tool/path sets match. Baseline
66.5593 seconds, candidate 38.7743 seconds; 105009 repeated paths versus
67932 unique paths, same 67932 distinct inputs. Raw status 0 is retained.
These single-run timings demonstrate this bounded reduction, not a rigorous
performance benchmark or platform boot-time comparison.

Every MMD edge still checks file shape and resolved ownership. Per-call
hashing/path-set reuse does not cache across independent admissions. Final
complete snapshot compares original output and ledger digests before first
sealing; causal stale/missing/alias/escape/post-verification mutation tests
remain. The real 180-second execution deadline is unchanged.

Diagnostic outputs remain locally at
/home/sean/current693-fwui-qemu.PMINOC/admission-receipts-diagnostic.u6z1q65c.
This finite packet does not independently archive all 67932 original build
inputs or claim new signed-current build, guest, PFP, hardware or project
acceptance. Fresh PR702 build and real save/reload validation are separate.
