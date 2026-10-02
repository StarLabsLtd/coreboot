# Incomplete authenticated apply and bounded late-PC diagnostics

This packet records failed/diagnostic runs, not a completed firmware update.
The genuine early RAM component reached the present-zero FMP-state prerequisite,
authenticated CHECK, and whole-media/state unchanged checks, then entered SET.
No SET completion, CLOSE, installed cold-image acceptance, or restore is claimed.

The source references are coreboot
`269901989920ec6ee9f3864f1f152ef51403fce1` (lease-local erased-padding programming)
and CDK2 `d3745376a42fc97f1ee5f95c84b9e49eda3c0dfd` (reviewed component fixture).
The production controls and normal 90-second acceptance deadline were not
removed. The copied secure pflash used explicit `cache=writeback`; that setting
already matches QEMU's default and is not established as a performance fix.
No host-power-loss durability claim is made.

## Recorded runs

The normal named apply run 75997 returned Make status 2 after the VM's
90-second timeout. Its GNU time receipt is 102.37 seconds wall, 97.41 user,
6.60 system, peak 256804 KiB. `raw/normal-90.log` preserves the aggregate
failure. Earlier 384-source run 10186 and a separately authorized 180-second
diagnostic also failed; extending a diagnostic deadline did not turn the
90-second gate into a pass. The latter used older 384 source, not 269, and its
copied `raw/older-384-180-time.log` records timeout 124, wall 180.05, user
139.16, system 54.70, peak 119472 KiB.

The late GDB wrapper 23427 completed its bounded collection (wrapper status 0),
while the underlying VM timed out with status 124. Its GNU receipt records
60.06 seconds wall, 56.96 user, 4.30 system, peak 119324 KiB. The same exact
owned execution copy and frozen component ELF were used, with secure pflash,
NVMe/xHCI/EDU, and no memory/register writes or authority changes. GDB only
paused, inspected, and resumed execution.

Actual SET_BEGIN was observed at Unix time 1790957662. Samples were scheduled
20, 35, and 50 seconds afterwards (16:14:42, 16:14:57, 16:15:12 UTC on
2026-10-02). The actual SMM symbol relocation was 0x3fe03000: nominal
`smm_handler_start` 0x8c1d resolved to loaded 0x3fe0bc1d. Top PCs were:

| Offset after SET_BEGIN | Loaded PC | Exact symbol |
| --- | --- | --- |
| 20 seconds | 0x3fe0f75d | udelay+38 |
| 35 seconds | 0x3fe3d59a | inventory+279 |
| 50 seconds | 0x3fe3ec60 | q35_dma_table_image_valid+667 |

These are three observations, not a statistical profile or a measured
dominance percentage. The deeper backtraces, arguments, and DWARF local values
are not reliable across the 64-bit QEMU remote register interface and 32-bit
SMM ABI; they must not be used to infer the writer block offset or a failed
inventory/table check. Equal erased padding and first differing target bytes
also do not establish progress through a programming loop.

Source inspection identifies repeated mutation-proof cancellation (including
the xHCI reset's mandatory 1 ms delay) at guarded media/source boundaries.
This motivates an independently reviewed exclusive-E8 lifetime investigation;
it does not authorize removing fresh controller, owner, inventory, translation,
physical-exclusion, or drain checks. No such follow-up mechanism is included
or accepted by this packet.

## External input identities (not copied)

- Execution-before ROM: `9b9a7cdba3e09963ede0d18b5f1154adc4d09ffafc3c5a9bc5d20f90371c627c`.
- Component ELF: `d0f603f586b1840c6f63d86f7f19b39f3ce8e1b0e1ed8e9bd8345f354c6e2b9d`.
- Actual SMM ELF: `3d5c722667c818ca802074b44eee28553008de8558ac3c34cb36b0fa25bcdd6b`.
- Pristine baseline target: `870d887b7ddd9d8bda113f34ce5858d243e21168261f33489310247dcd37d18a`.
- Pristine newer target: `79e979c5ffacffe509fb25426937575ba7a01bca29252e86ef6b2b4e71ea08b7`.
- Newer authenticated image: `b2c55b36a5558137f2e0fc0671bffc0896d3476959fd2be138bf3654ab3e86bf`.

Only bounded text receipts are copied. Guest media, ELF/ROM images, private
signing keys and combined private signer files are excluded. Source references
remain independently retained signed Git checkpoints. Native install/restore,
production SystemFmp full-graph execution, typed disk lifecycle, hardware
compatibility, and host-power-loss durability remain unproved here.
