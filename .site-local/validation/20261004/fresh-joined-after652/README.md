# Fresh admission and joined HOST evidence through CDK2 PR652

This packet preserves selected original text receipts, scripts and qualification
notes. Collector exclusions omit executables, ROMs, raw media, framebuffer data
and copied imported source trees. ORIGINAL_FILES.sha256 identifies the original
local files; ARCHIVE.sha256 binds the archived packet. Copies were byte-compared.

Published source:

- PR650, 791eee17549dd1014ad94710c2a991f95d17d1a9: explicit fresh normal
  admission for pristine UI-cancel and Linux-origin reset guests. Its final
  refrozen HOST packet passed; prior failed exploratory attempts remain local.
- PR651, 01729b589f769c4f91b70342522a91dfe3fb4aa5: use the registered public
  renderer Make goal. HOST admission/invocation checks passed. The earlier
  receipt-only preflight failure is preserved alongside the corrected run.
- PR652, 14384fb9609ae158164dcbcc73126da7e568f1db: reviewed runtime tests,
  readiness fixture repair and dedicated DMA-safe TPM-absent oracle. Joined
  HOST aggregate0: four TPM/acceptance selftests, capture readiness and four
  fresh/historical/setup-controller/Linux-reset model suites; source/tools and
  HEAD unchanged. Runtime sanitizers have separate author/independent packets.

Neither actual fresh firmware attempt passed:

- ready650 failed at top-level dispatch of an internal renderer executable
  target. Aggregate1; no firmware/guest qualification.
- ready652 built native Core and passed its ELF/MTRR and LVGL provenance,
  form, framebuffer restore and splash-status checks. Native build exit0,
  116.52 seconds. Overall aggregate1, 211.99 seconds: producer tool capture
  attempted to read xcompile after configuration-only Make deliberately skipped
  generating it. No producer firmware compilation, ROM seal or guest followed.

Both failed build directories remain unchanged. These are bounded HOST/native
results, not latest-guest, TPM/PCR, hardware, full-suite or project sign-off.
The source-owned preparation ordering fix is under separate review and gates.
