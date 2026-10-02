# Frozen be113 owned-code style baseline

Exact clean source: `be113da9b9ef890f3249b3b80539dfa960544f0f`.
This is a read-only baseline, not whole-project style acceptance.

The tracked C/header/Kconfig inventory covers root Kconfig and src/include/
tests/util: 886 paths. Fourteen tcg_hash/vendor paths are excluded from the owned
style run only after the actual hash-bound software_hash_source_test verifies
their unchanged provenance. The owned software_hash adapter remains included.
All 886 source hashes and the exact Git HEAD agree before/after the run.
Submodule/imported third-party sources outside that inventory are not rewritten.

The actual existing cdk2-checkpatch filter was compiled once unchanged and used
with its normal FD identity contract for four parallel independent file checks;
there is no added suppression, allowlist or relaxed checker. Each path retains
its raw output and actual exit status. The collector reaped 0 (session96954)
because collection completed; this MUST NOT be described as a lint pass.

Results: all 872 owned files completed, 812 checker exits 0 and 60 exits 1.
Raw diagnostics total 64 error records and 80 warning records. Error records:

- POINTER_LOCATION: 36
- TRAILING_STATEMENTS: 17
- SPACING: 7
- FLEXIBLE_ARRAY: 3
- ELSE_AFTER_BRACE: 1

The 64 error records split src19/include5/tests37/util3. These are checker
records, not yet 64 independently adjudicated source defects: known ABI-attribute
spacing parser cases require parser analysis, and the three FAT one-element
array findings require wire-layout review rather than blind formatting. The
baseline does not establish that every private type already meets the brief.

Separately, the actual fresh stable lint reaped 1 (session96537): all checks
except native-boundary passed. Its one failure is the new diagnostic dependency
include `$(CDK2_PUBLIC_FULLGRAPH_APP_OBJS:%=%.d)` at src/boot/Makefile941, not a
claim that the diagnostic executes forbidden legacy tooling. The subsequent
root dependency cleanup is a different source and is not a pass for be113.
The stable whitespace producer emitted binary-match notices; its own check
reported success. Unedited actual output is stable.log.

The first provenance invocation omitted the required root argument and reaped2;
vendor-provenance.log retains that failure. The corrected exact test reaped0
before the owned scan, retained separately as vendor-provenance-retry.log.

alias-occurrences.tsv is a comments/strings-stripped src-tree C inventory of
15167 UEFI aliases across 262 files. It includes source fixtures and genuine
public/wire boundaries; these counts are explicitly UNCLASSIFIED occurrences,
not 15167 private-type violations or a production compiled-input inventory.
Actual public ABI aliases must remain unless independently justified. This
packet is not hardware, complete compiler-input, semantic-cleanliness or
whole-project completion evidence.
