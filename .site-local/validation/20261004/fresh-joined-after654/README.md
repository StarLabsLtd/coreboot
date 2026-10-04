# Fresh source-built Q35 evidence after ready654

Canonical CDK2: signed `0bb127b503d8c5d973b07d608f564ff0ef2ef6bb`.
Coreboot producer: signed `7ee34bed989c46913c3ee6672fb25e83227c3b6c`.

The fresh source-built ready654 build completed with aggregate status zero.
Core SHA-256: `109d047a6507565db60567ebda8b2ea84f71d8134ecfc24de7bbf6fa357f9001`.
ROM SHA-256: `ab4e77ec30d1016ce66dc2d7e4987e8e746603923564680dd3307fbf312ab38a`.
Fresh descriptor SHA-256: `ab7a8ba1b835299e8a8b50e9252c3a2b749503d9310732afaec182b24dbd483b`.
Build wall time was 306.56 seconds; the earlier successful ready653 build took
607.90 seconds. These are host build/seal times, not firmware boot timings.

Two independently launched pristine copies of that ROM passed their bounded
guest lifecycles and owner closure checks. UI edit/discard took 86.4875 seconds
inside the unchanged 180-second run-one budget. Linux-origin firmware setup
reset/return took 133.864385 seconds inside the same budget. Outer totals
(including receipt replay and media checks) were 253.58 and 269.36 seconds.
No host-generated reset or old firmware substitute was used.

Root and an independent reviewer inspected the captured frames. Existing O0
and O2 media codecs were independently replayed read-only, with both raw exits
and aggregate/closure statuses zero. The expected negative codec mutants abort
with status 134; those aborts are not guest failures. Whole protected-store
comparisons cover the complete store; hostile mutation samples are not a
claim that every media byte was independently mutated.

Visual inspection also found an open accessibility defect: keyboard focus
changed help text without visibly outlining the focused control. FOCUS-AUDIT
is retained. Passing edit/discard does not sign off all setup pages, saving,
hotkeys, Secure Boot controls, or accessibility.

The full standalone DEFAULT regression stopped at its first inventory test:
legacy_boundary_inventory_test referenced the pre-direct-LVGL historical audit
for the later-added direct LVGL gitlink. Actual make exit was 2, closure was 0,
and aggregate was 1. The preceding review-profile checks passed. This failed
run does not constitute a passing full native suite or firmware regression.

Selected original text receipts and lossless PNG previews are archived here.
Files larger than 1 MiB are losslessly gzip-compressed with deterministic
headers; decompression was compared with the original. ORIGINAL_FILES.sha256
binds original absolute paths and original bytes. ARCHIVE.sha256 binds stored
files. Neither list claims that older source snapshots match a subsequently
advanced canonical checkout. No ROM, ELF, installed EFI executable, disk,
raw protected store, or imported source tree is included.

No hardware was touched. TPM present/absent lanes, the complete regressions,
remaining feature matrices, and final hardware validation remain open.
