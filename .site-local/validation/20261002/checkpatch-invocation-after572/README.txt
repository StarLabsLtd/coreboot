Checkpatch compile-once receipts, PR572
=====================================

Reviewed source: d3d6af279a5be9da7b4597017f914804596a47a9, signed G.
Historical complete lint source: c1e16e4c58aa2ddfdcf0b4f94feaab20dc844ef5.
Source archives were exported afterward from those exact commits. No submodule
closure, ROM, private key or guest media is included.

The unchanged filter is compiled once after finding a nonempty file selection,
then passed using the existing descriptor-bound mechanism. The compiler-call
traces show four baseline invocations versus one for the four-file driver;
raw diagnostics and aggregate result match. Empty/diff/failure cleanup and
the adapted real-filter launcher are covered by the committed permanent gate.

Root focused initial gate 76524 exited2 because the launcher fixture omitted
the newly required actual filter source; preserve that failed log. Its fix
copies the real filter and mocks the existing checkpatch child instead. Final
joined-parent focused gate 62798 exited0, including all11 Core RAM HOST runner
cases. This is neither a production capsule boot nor complete final lint.

Historical complete canonical lint session20577 exited2 (GNU19:03.05), with
all stable lint checks passing but checkpatch returning761 errors/26 warnings.
The optimized checker-only sweep session68526 reaped outer0 only because its
wrapper explicitly expected the real checker result1; checker GNU exit1 and
12:40.59 are retained. It reports the same761 errors/26 warnings, not clean lint.
These times describe different enclosing commands, not a controlled firmware
performance comparison or a guaranteed percentage speedup.

That optimized sweep selected the original PR570 source inventory while its
source worktree was at 5fb9cf32bf410ab6931ee8d3a42e28e1902bb2b9. Root later
restacked that worktree onto PR571/572 during the read-only sweep. All four
bound checker/filter/ledger/parser bodies and every originally selected source
body remained equal, and the four before hashes checked OK after completion.
The new PR571 native fixture files were not in the original selection. This
receipt is expressly NOT a full sweep of PR571, PR572 or the final tree.

The compiler-failure branch aborts before traversal rather than repeating the
failure per file; identical compiler-failure aggregate statuses are not claimed.
No parser rule, allowlist entry or finding was suppressed by this speed change.
