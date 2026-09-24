# MOR live-DRAM inventory

`PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY` builds a fixed, allocation-free MOR
clear plan from `bootmem_walk_dram()`. That walker is the sole memory source:
ordinary `BM_MEM_RESERVED` entries outside authoritative device-domain DRAM
are never inferred to be memory.

The composer maps final `BM_MEM_RAM` intersections to `CLEARED` spans. Final
`BM_MEM_RAMSTAGE`, `BM_MEM_TABLE`, and `BM_MEM_PAYLOAD` intersections become
`EXCLUDED/ACTIVE_FIRMWARE`; every other valid final bootmem tag becomes
`EXCLUDED/PLATFORM_RESERVED`. Platform-supplied exclusion overlays split those
ranges exactly. Overlays must be nonempty, nonoverlapping, overflow-safe, and
covered byte-for-byte by authoritative DRAM. The composer accepts at most 32
raw spans and delegates sorting, merging, and the 15-span limit to the existing
canonical plan builder.

The input request, live range order, overlay coverage, output-zero state, and
inventory token are rechecked before publication. Unsupported tags, malformed
ranges, source overlap, incomplete overlay coverage, excess fragmentation, or
callback mutation fail with a zero output.

The StarBook Meteor Lake adapter derives the inventory token from the prepared
DMA-guard generation and identity. It overlays the live handoff, translation
table, and table mirror as active firmware, and all three requester arenas as
platform-reserved memory. It then applies the existing DMA-guard policy check.
Its executor callback recomposes this inventory without allocation and requires
an exact match with the supplied canonical plan immediately before clearing and
again before publication.

This code installs no boot-state hook, binds no DMA guard, clears no memory,
publishes no endpoint, and makes no platform-support claim.
