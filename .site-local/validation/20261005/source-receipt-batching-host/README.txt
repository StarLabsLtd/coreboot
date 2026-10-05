Serial tracked-source receipt batching — bounded HOST evidence, 2026-10-05

Signed source a1cc4f88e3dfe8902a28042bbaa4af68f11c5266, PR703;
parent ba629cf33fb4fa99e33989ff557b45877c874ee1, PR702.
Reviewed exact three-file delta SHA256:
b765753137245c16a3f7a161319431f56a5523ec3075393f79fb582562327a7c
Root original records:
/home/sean/current693-fwui-qemu.PMINOC/source-receipt-host.sR2J8ErG
Author original final fixture records:
/home/sean/current693-fwui-qemu.PMINOC/source-receipt-author-host.oETWGjQH

All 40 records are literal originals; this README is separate. Thirteen
Root raw statuses are 0: five suites (16+11+5+4+51 = 87 tests), stable16
lint, empty Bash syntax check and six actual-source receipt invocations.
Author final16-test status 0 is independent of Root's final suite.

Actual full ordinary before/after receipt bytes compare exactly for:
firmware1648 rows, producer25087 rows and regular-mode vboot1292 rows.
Timings respectively 4.12 -> 0.23, 57.36 -> 1.50 and 3.54 -> 0.70 seconds;
all six logs empty and time/status records report 0. These are single-pair
diagnostics on real tracked sources, not a rigorous benchmark, full builder
execution, new guest or hardware validation.

The NUL-delimited serial pipeline retains Git stage order and exact all/
regular mode filtering. Actual mini-Git fixtures test executable, regular,
symlink and gitlink modes; 513 regular files prove multiple serial batches;
empty inventories do not invoke hashing. Git, xargs, hashing and a missing
last-batch file all fail nonzero. Space/tab/newline/backslash paths are
addressed literally; GNU escaped receipt rows remain rejected by the
unchanged strict downstream parser. No expanded unusual-name admission or
identical predecessor failure code/behavior for every unusual name is claimed.

The existing finite build tool inventory now binds xargs resolution and
bytes, requiring all 19 logical tools. Missing/unbound/alias xargs cases
refuse. No parallel hashing, cross-run cache, source seed, FV tooling or
guest-deadline change is introduced. Fresh PR703 build acceptance remains
separate from these bounded HOST gates.
