# Exact-head lint checkpoint

Frozen CDK2 source: `4ba3da5839202746c9137cde1e875064753ebc87`.
Managed session 8805 actually returned 1 after 14m35.758s (CPU 14m36.588s).
The exact commands were `util/lint/lint lint-stable` followed by
`util/lint/lint lint`, with `TMPDIR=/home/sean`. Both returned 1.

The broad raw stream contains 1,298 checkpatch error starts and 149 warning
starts, across 84 named files. It includes imported, hash-bound vboot/Linux
source as well as owned production and test files. These are not an owned-only
baseline, nor a justification for editing imported code. The stable failure
was static boundary-checker rejection of four `filter-out` source expressions.
The broad run also includes that stable failure.

Subsequent reviewed fixes are recorded separately, not applied retroactively
to these raw failures:

- `d2fac4d96d17078aff5daea1e1aed239e1921af2` conservatively inspects both
  `filter-out` arguments, retaining forbidden excluded sources. The complete
  regression and independent replay passed.
- `15c374f14c5359b5101e767232397a36e258eef8` fixes fourteen literal pointer
  spacing defects; `git diff -w` is empty. Remaining findings were not waived.
- `f8f609ca971d8b4fff746a06f8d7563dbf0402bb` teaches checkpatch seven genuine
  existing typedefs. Every added type still rejects malformed pointer spacing;
  the complete filter/launcher/allowlist gate returned 0 in 31.845s and an
  independent replay passed.

The corrected stable suite returned 0 (session 94504); its raw output is
retained here alongside the earlier failures and parser regression output.
Full-tree style and private/native type conversion remain open. A clean stable
suite is not a complete style audit.
