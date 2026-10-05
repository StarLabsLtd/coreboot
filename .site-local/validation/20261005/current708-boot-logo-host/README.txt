PR708 private boot-logo native types — 2026-10-05

Signed source e65c2d63a4eeaa7b14deac5626a8f347151bb92f, parent
c1dba725d3734bb1f737c6ec08b4cec68e16967e. The sole source delta is
src/lib/boot_logo.c, binary-diff SHA256
cafe1fb27a67ad22183fb054a7fa85e0ba76732ca4569521b7799395d5978b57.
The public header, signatures, layout, wire data and EFI status boundaries
are unchanged. Private native scalar types preserve the current x64 ABI;
there is no 32-bit portability or hardware claim.

Archive roots retain the complete actual bounded HOST gate tree and the
KEEP_SPLASH_STATUS_TMP output of the existing public status-report test.
This preserves compiled binaries, generated configs, source-derived parent
copy, actual compiler argv, dependency/input ledgers, models and raw logs.
External signed source, compiler and vendor owners remain required.

Public native-early-splash-test passes independently at O0 and O2 with
strict warnings, ASan/UBSan and no recovery. The public status-report gate
passes at debug0/1 and O0/O2 using actual DXE callbacks and its existing
coupled source mutants; elapsed 111.66 seconds. Native-stage and real LVGL
renderer/provenance/form/restoration tests pass; elapsed 23.74 seconds.
These are HOST workflow times, not firmware boot times.

The local byte-parity wrapper includes the actual early_splash_test.c,
runs its existing checks, then emits its guarded framebuffer and static
BMP buffer. Parent/current at O0/O2 produce the same 42029 bytes, including
29 bytes of existing PASS text, 33808 framebuffer bytes and 8192 BMP bytes.
All four outputs have SHA256
c9b863b7b83b6d55b6a6023da6e54ea4624694b38ba61c077433dee8d40700f1.
This is bounded fixture parity, not exhaustive pixel geometry coverage.

Owned-file checkpatch returns zero with zero errors/warnings but 28 CHECKs
(standard integer-type suggestions and existing continuation alignment).
The nonempty raw report is preserved; no entire-repo style-clean claim is
made. Opposing reviewer inspects all actual compiler argv and source/input
ledgers, public-header equality, signed parent bytes and artifact parity.
One reviewer path-token assertion needed normalization for an existing
double slash; that was a review assertion, not a failing firmware test.

This closes the one-file conversion and bounded HOST rendering gates.
No new QEMU guest, whole-project completion or hardware acceptance is
claimed. Current originals are retained; this is not cleanup permission.
