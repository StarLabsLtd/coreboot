# Bounded payload DMA handoff

`PAYLOAD_DMA_HANDOFF` is a default-off, preparatory interface for transferring
an already-active DMA-deny state to a payload. It does not configure an IOMMU,
enrol a controller, enable PCI bus mastering, or claim production readiness.

The platform prepares a compact revision-1 blob in `CBMEM_ID_DMA_HANDOFF` and
returns its address through `payload_dma_handoff_blob()`. The generic producer
validates exact sizes, flags, generations, BDFs, domains, page-table ownership,
indices and non-overlapping resident table ranges. It emits
`LB_TAG_DMA_HANDOFF` only after a revision-4 payload resource handoff completed
successfully. A missing or malformed blob is fatal when the option is enabled.
The 26.09 base supplies fail-closed weak resource-publication hooks; a platform
must override them from its revision-4 resource producer before enabling this
option.

Revision 1 permits at most two requesters and five table descriptions. Unknown
flags, nonzero reserved fields, duplicate BDFs or domains, unreferenced tables,
and inconsistent ownership are rejected. The reference and the generic CBMEM
directory entry must identify the same exact allocation; this is checked by the
consumer. The fixed bounds are contract limits, not a topology claim.

On Meteor Lake, `ENABLE_EARLY_DMA_PROTECTION` also selects FSP's pre-boot DMA
policy and reapplies the existing VT-d PMR protection after memory init. This is
only a platform prerequisite. It does not produce this handoff or prove that a
particular payload or capsule buffer is covered by every DMA-remapping engine.

Run the producer checks with:

```
tests/lib/dma_handoff_standalone_test.sh
```

They exercise ordinary, optimized, ASan+UBSan, mutation, publication-gate and
canonical 244-byte fixture paths.
