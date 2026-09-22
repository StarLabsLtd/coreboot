# Bounded payload DMA handoff

`PAYLOAD_DMA_HANDOFF` is a default-off, preparatory interface for transferring
an already-active DMA-deny state to a payload. It does not configure an IOMMU,
enrol a controller, enable PCI bus mastering, or claim production readiness.

The platform prepares a compact revision-2 blob in `CBMEM_ID_DMA_HANDOFF` and
returns its address through `payload_dma_handoff_blob()`. The generic producer
validates exact sizes, CRC, flags, generation, exact BDFs, isolated protection
domains, and one immutable, pre-mapped DMA arena per requester. Each arena
records its CPU physical base, device-visible IOVA base, page count, coherency
and read/write permissions. Translation tables and other backend geometry are
private to the coreboot IOMMU driver. The public record asserts that coreboot
owns an active default-deny mapping until ExitBootServices. It emits
`LB_TAG_DMA_HANDOFF` only after a revision-4 payload resource handoff completed
successfully, and every requester must match an exact revision-4 boot-intent
BDF whose snapshotted PCI command has bus mastering clear. A missing or
malformed blob is fatal when the option is enabled.
The 26.09 base supplies fail-closed weak resource-publication hooks; a platform
must override them from its revision-4 resource producer before enabling this
option.

Revision 2 derives its requester bound from the revision-4 PCI topology limit,
currently 512 entries. The serialized requester count may not exceed the
published boot-intent count. Unknown flags, nonzero reserved fields, duplicate
BDFs or protection domains, zero, misaligned or overflowing arena ranges, and
CPU arenas overlapping each other or the serialized handoff are rejected. IOVA
ranges are protection-domain-local,
so equal IOVAs in distinct isolated domains are valid. The payload may use only
a requester's described arena and must not reprogram the IOMMU.

The generic validator cannot prove that an arbitrary physical arena belongs to
a particular CBMEM allocation: the current typed memory-policy section covers
the framebuffer delegation, not DMA-arena ownership. Each backend must therefore
prove that every arena is wholly contained in its own reserved allocation before
returning the blob. The payload independently requires those ranges to remain
reserved in its imported memory map. The reference and generic CBMEM directory
entry must identify the same exact handoff allocation; this is checked by the
consumer.

On Meteor Lake, `ENABLE_EARLY_DMA_PROTECTION` also selects FSP's pre-boot DMA
policy and reapplies the existing VT-d PMR protection after memory init. This is
only a platform prerequisite. It does not produce this handoff or prove that a
particular payload or capsule buffer is covered by every DMA-remapping engine.

Run the producer checks with:

```
tests/lib/dma_handoff_standalone_test.sh
```

They exercise ordinary, optimized, ASan+UBSan, mutation, publication-gate and
one, two, three, topology-limit, CRC, hostile mutation and canonical 104-byte
fixture paths.
