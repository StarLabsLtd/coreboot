# DEFAULT656 regression and targeted loader HOST retry

The original signed-head DEFAULT656 run remains failed: Make/execution status
2, closure 0, aggregate 1. Its unmodified `-j1 -k` run took 2421.65 seconds.
The complete raw log preserves two missing-PATH loader errors, the protected
boot-gate fixture assertion, the RAM-window report linker failure, and the
capsule-report configuration-cache registration failure. Expected assertion
mutants elsewhere are not counted as baseline failures.

The separate loader retry adds installed `/usr/sbin` and `/sbin` tools to PATH;
it does not change firmware or tests. Both unchanged HOST loader matrix scripts
pass, with execution, closure and aggregate 0. Existing real generated headers
and source-built enrollment/probe EFI files are hash-bound. These checks prepare
and validate loader media; they do not boot loaders in QEMU or on hardware.
They do not turn the original whole DEFAULT run into a passing run.

This finite packet includes only immediate-root text receipts from the two
completed stages, the exact outer recipes and the collector. It excludes all
build/tmp descendants, source trees, objects, ROMs, EFI executables and media.
Original files remain intact. Large text files use deterministic lossless gzip;
the original hash ledger and stored-file hash ledger have different purposes.
Original closure checks completed before the legitimate canonical advance to
ready658; replaying historical source hashes against newer source is not valid.

The normal ready658 build and subsequent HOST/QEMU work were still separate
in-progress work when this collector was prepared. No final-project, TPM,
hardware, or full-regression pass is claimed by this packet.
