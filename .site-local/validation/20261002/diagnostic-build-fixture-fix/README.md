# Diagnostic build contracts and capsule fixture layout

Signed root build repair `031411e9621491dea1cd760345f007e64f05c8eb`
and fixture layout `d7cd7b4712b214fd2c768c03207e8a3bedb176a0` are ready
CDK2 PR503, parent `be113da9b9ef890f3249b3b80539dfa960544f0f`.
Both changes are independently source-reviewed; the runtime production guard
and native variable implementation are unchanged.

The full selected `check native-stage` at be113 actually reaped 2 in root
session 12679 (15m57.815s service elapsed, 32m30.971s CPU, peak 1G).
Its single reported Make failure is native-config-cache-test: the new diagnostic
PE and linked object omitted the existing native configuration/command contract.
The normal source-linked image and the remaining completed native gates passed;
that does not turn the aggregate into a pass. The raw failed log is retained.

The repair registers diagnostic artifacts with the existing config/command and
PE-tool inventories, declares tools before early prerequisites and removes a
redundant dependency include already covered by native `*.d`. Two intermediate
standalone app builds exposed missing link-helper and relocation-marker inputs;
those were fixed in source, not supplied externally to disguise a dependency.
Actual build from an empty directory `cdk2-public-app-cold-fixed.f6Goa2` reaches
the checked PE. Its raw log is retained; an earlier combined lint attempt then
failed because the formatter emitted two space-indented closing braces.

At the final body, session 92634 actually reaped 0 for configuration-cache,
command-cache, transitive host dependency, BDS and actual DXE disk/causal gates.
Session 70942 actually reaped 0 for full stable lint; session 24650 reaped 0
for three complete fixture checkpatch invocations (silent success).
The final manual brace/layout corrections address genuine formatter findings.

All three fixture preprocessed bodies compare identically after canonical
formatting. `__LINE__`/`__FILE__` are neutralized only for this comparison,
never for executable tests. A peer independently compared ordered source tokens
with C line splicing. Initial and final source copies/comparison output remain
in `/home/sean/capsule-test-format-proof-20261002`; this packet is not the full
compiler input closure or hardware/whole-tree style sign-off.
