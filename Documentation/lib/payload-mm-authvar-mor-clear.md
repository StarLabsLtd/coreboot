# Payload-MM MOR clear plan and receipt

`PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT` builds a dormant, pure planner and
completion-receipt gate. It performs no memory write, cache operation, zero
readback or DMA transition. It adds no boot hook, platform selection, CBMEM or
SMM transport, endpoint, producer, authenticity claim, or MOR support claim.

The planner accepts up to 32 pointer-free physical DRAM spans in any order. It
rejects zero or overflowing ranges, overlap, unknown classes and exclusion
reasons, then sorts and merges adjacent spans with identical class and reason.
Physical-address gaps are legal: they may be MMIO or other non-DRAM space and
are not silently treated as excluded DRAM. The caller asserts that the exact
listed spans form the complete DRAM inventory. The planner does not prove that
assertion. Failure to represent the canonical result within the grant's
15-span bound fails closed. The nonzero inventory generation and opaque
256-bit identity are assertions supplied by a future trusted producer;
revision 1 defines no hash algorithm or canonical preimage.

The receipt builder requires one record for every canonical `CLEARED` span in
the same order. Each record repeats the exact base and size and provides exact
written, cache-writeback-fenced, and full-zero-readback byte counts; every count must
equal the span size. Exclusions come only from the exact canonical plan, so the
transcript cannot redefine them. The transcript repeats the exact entry
request, cold-boot generation, inventory generation and identity, and two
explicit DMA-policy snapshots taken before and after clearing. Each DMA
snapshot has a nonzero generation and opaque nonzero 256-bit identity, and the
two complete snapshots must be byte-identical. All unused records and reserved
bytes are zero. Only exact agreement produces a completion grant accepted by
the grant's own validator.

Inputs are snapshotted without altering the copies used for mutation checks and
checked again before the single publication. Inputs and output must be valid
aligned, pairwise-nonoverlapping native objects. A valid output object is
zeroed before any semantic check and remains zero on error. These mechanics
detect malformed or changing input but do not authenticate the assertions. A
future platform executor must establish cold-boot freshness, protected DMA,
the reviewed complete inventory, actual writes, cache persistence, full zero
readback, and protected SMM installation before MOR can become supported.
