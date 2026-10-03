Unused executable-FV discovery cleanup

Source: /home/sean/Documents/.cdk2-worktrees/unused-fv-discovery-after584
Baseline: signed 0de72a0eff2f263056839eb206641f59757b7a8d (PR584).
Four paths: delete src/boot/fv.c and fv.h; remove all corresponding object,
dependency and HOST-test recipes; replace the deleted header-lint injection
fixture with existing src/boot/pe.c. Variable FV/FVB formats and codecs stay.

Non-author style worker reviewed the complete deleted parser/header, all
remaining production/build references and header-lint source, with no blocker.
The retained direct-DxeCore bridge test still checks finder symbol ABSENCE.

Initial named.log/time failed before tests because the top-level wrapper does
not expose native-dxe-core-fv-protocol-test. This is a preserved invocation
failure, not a source/compiler or firmware failure. named-retry.log/time is
the actual corrected normal Make invocation using a copied resolved config,
normal Kconfig generation, private outputs and pinned read-only dependencies.
Actual corrected process 54270 reaped exit 0. native-coreboot-test,
native-stage and native-variable-runtime-test passed. The variable runtime
suite includes explicitly modeled transport/HOB cases and causal mutants;
expected aborts from those mutants are not native firmware crashes.
GNU elapsed: 2:42.00, user 177.45, system 21.84. The copied resolved config
still compares exactly with the original genuine resolved input.

Root header injection/reset completed with exactly four observed violations
in header-observed.log and all four before-SHA checks matching after reset.
Style independently repeated both fallback and Git-grep paths on an isolated
copy: /home/sean/unused-fv-header-lint-peer.k3o3uw.

The actual built native/cdk2-stage.elf has no cdk2_native_find_dxe_core symbol
and native/fv.o is absent. No whole-binary parity, new QEMU, hardware, complete
profile matrix or entire-project lint/acceptance is claimed here.

Actual process 55508 reaped 0: DxeCore FV protocol test using the same actual
generated configuration. Project patch checker exited 0. After restack onto
signed PR586, actual process 91687 reaped 0 for native-coreboot-test and
native-stage. Published ready PR587 is signed commit
81478b02d4dd02ff6a98d749c1784842109aefbe; all four cleanup bodies unchanged.
