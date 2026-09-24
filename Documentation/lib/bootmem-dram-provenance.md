# Bootmem DRAM provenance

`BOOTMEM_DRAM_PROVENANCE` retains the original cacheable memory resources from
enabled `DEVICE_PATH_DOMAIN` devices before bootmem applies reservation and
firmware-ownership overlays. `bootmem_walk_dram()` later reports allocation-free
intersections between that immutable source domain and the final bootmem map.
Each reported range has its final `bootmem_type` tag. Provenance retains exact
byte bounds rather than bootmem's conservative page-aligned expansion.
Initialization verifies that every authoritative source resource is covered by
the retained union and stops boot if that invariant cannot be established.

The provenance map deliberately does not infer DRAM from final tags. In
particular, an arbitrary `BM_MEM_RESERVED` range may describe MMIO and is not
reported unless it intersects an original cacheable domain memory resource.
Likewise, a cacheable resource attached to a non-domain device is not accepted
as authoritative DRAM provenance.

The option is default-off. It adds no inventory consumer, memory-clear hook,
endpoint, or platform support claim.
