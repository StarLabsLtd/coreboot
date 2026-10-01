# Router, namespace and harness checkpoint

These are actual root command receipts, not a project-completion claim.
Ready CDK2 PR455 is `b1fc2b1f13607b2e801f240d3ef5b3610ceb5a8c`;
PR456 is `739767908f5504832f93048f62e6894129925c87`, directly following it.
`ready455-456.patch.gz` preserves both signed source commits without changing
the format-patch bytes.

The complete harness initially returned 1 at its retired PCD assertion, then
2 at its deleted FV-only inventory script. The retained `selftest.log` and
`selftest-pcd-fix.log` record those failures. The native-inventory repair run
returned 0; the separate `b1fc-exact-head.log` command returned 0 at clean
PR455 source, taking 2m33.768s. Configured patch checkpatch returned 0 with
zero errors/warnings. This does not establish full-tree style cleanliness.

The PR456 namespace component returned 0 with O0/O2 sanitizer runs and four
expected assertion-abort causal mutants. Its configured patch checkpatch
returned 0 with zero errors/warnings. The subsequent continuing whole
`check native-stage` command at clean PR456 returned 2 after 10m21.599s:
`native-host-dependency-test` could not generate its generic router mutant,
because it counted all `return status;` statements after canonical policy
methods were added. Other checks continued under `make -k`; the aggregate
is a failure, not a pass. A bounded function-scoped generator repair is under
independent review and test. An initial repair command also returned 2
because its systemd service lacked the working directory; it did not test
the source, and its log is retained separately. The first actual repair run
returned 2 because a quoted multiline sed expression retained a literal
backslash. The revised multi-expression sed form is independently reviewed;
its actual dependency gate returned 0 after 1m14.735s, including real compiler
input unions, deliberate compile failures and transitive cache rebuilds.
It is signed commit `ac3d8acfa42ceb1830abc390bfc23d48030ea1fb`, directly
following PR456. `scoped-mutant-ac3.patch.gz` preserves the exact source.
The failed recipe receipts are retained. The repair's focused pass does not
turn the earlier aggregate failure into a whole-suite pass.

`root-whole.config` is the continuing resolved configuration, SHA256
`8a70dc70dc9cb75849aa1bd9cc15fa472a22a1a9d131a011b38a290130cdba46`.
`root-component-config.h` is its generated input header. The isolated
canonical-core command at signed WIP
`5076371d79a30aed41db1bb93713a9dc4a7cb0f9` returned 0. Its script explicitly
overrides protected-variable selection in a temporary component header and
uses modeled host interrupt hooks; this is not Kconfig admission, native
CLI/STI execution, QEMU activation or hardware validation. Opposing review
subsequently found private publication/diagnostic and public Start/Unload
windows. That WIP is therefore not accepted or production-integrated on the
basis of this component pass. Corrections and exact causal tests remain open.

No new firmware image was booted for these receipts. The separate PR453
QEMU archive remains the actual compiled/booted compatibility image; it must
not be relabelled as PR456 or protected-policy activation. Hardware,
production entry lifecycle, broader crypto/platform work and style remain open.
