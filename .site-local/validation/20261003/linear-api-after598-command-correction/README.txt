Additive correction to the published PR598 receipt wrapper
=========================================================

The original 84-file packet and archive remain unchanged. Its historical
publication is evidence commit a086c809f75fbfefc24bdc39ad432509de0d8269;
archive SHA1badc4efe0b1c4fe50113b6bc336be61bfeaba05b72295c67cda719586391b1a.

Correction: the final Reconstruction paragraph's sentence claiming named
family logs retain real compiler commands is too strong. Those normal Make
recipes are quiet; the logs preserve actual outputs/status-related receipts,
not complete compiler argv or a shell execution transcript. Replace that
sentence when interpreting the historical packet with:

  Named-family logs retain actual gate output and generated artifact paths.
  The signed Make recipe archives in this additive correction reconstruct
  the compiler commands; they are not newly discovered original argv logs.

Both exact signed Makefile/src/boot/Makefile archives are included: baseline
596 used for the main family gates and final598 for the pinned linear retry.
The native HOST compiler helper intentionally starts with @ and redirects
generated dependency output, so lack of printed compiler argv is expected.

replay.sh is an explicit reconstructed normal replay invocation, not a saved
original command transcript. It retains the known real family target lists,
normal config resolution and explicit read-only BSSL/LVGL dependencies. Give
it a new absolute output root and a copy of the historical named resolved
configuration; it does not alter the original artifact directories. P0 uses
five supported families, P1 also adds certified-refusal. The original packet
already records the actual proof-vs-named P1 profile distinction.

No raw logs, statuses, timings, original hashes, source bodies, object/byte
comparisons or gate qualifications changed. No compiler replay, new native
boot, authority grant or whole-project completion is claimed by this fix.
