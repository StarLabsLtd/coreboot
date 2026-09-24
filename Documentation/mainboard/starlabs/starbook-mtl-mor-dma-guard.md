# StarBook Meteor Lake dormant MOR DMA guard

`STARLABS_STARBOOK_MTL_MOR_DMA_GUARD` builds a dormant, synchronous DMA-live
guard for a future MOR executor. It adds no boot-state hook, endpoint, clear,
completion receipt, or platform-support claim.

Meteor Lake exposes two remapping engines relevant here. VTVC0 is the
include-all engine and is transitioned from FSP protected-memory ranges to a
coreboot-owned default-deny translation root. GFXVT is the IGD engine. The
guard does not invent a third IPU engine: DMAR describes IGD and IPU as SATC
requesters, while IPU remains under VTVC0's include-all scope. Revision 1
therefore requires both integrated requesters to be present in the exact
segment-zero ECAM snapshot and verifies their Bus Master Enable bits are clear.
The enabled GFXVTBAR and its actual base are read back from MCHBAR.

The existing DMA-live transition retains the exact PCI identities and original
non-BME command bits for every discovered function. Every capture rechecks that
complete topology, BME-clear state, VTVC0 registers and root, GFXVT registers,
arena geometry, and all translation-table bytes. The FSP reservation is
partitioned exactly into the handoff page, live translation table, and three
contiguous arenas. The table's equal-sized integrity mirror is a separate
page-aligned CBMEM allocation below, and never aliases the FSP reservation in
either virtual or physical space. Both table regions are zero-initialized,
fixed-bounded, and compared over their full capacity so unused-tail changes
also fail closed. Every observation independently re-anchors both regions to
the current FSP DMA-buffer result and current CBMEM directory entry.

The transition's trust order is deliberate. Initially the FSP DMA buffer starts
immediately above, and therefore outside, the active low protected-memory
region; the CBMEM mirror is below that boundary and inside the low PMR. The
implementation clears and reads back Bus Master Enable across the complete
ECAM inventory before it zeroes the buffer or constructs any table. It then
builds and mirrors the image while the mirror remains protected, switches
VTVC0 to the default-deny root, and rechecks all table bytes. Under that root
neither the live table nor its mirror is mapped for a requester. Finally it
reverifies the complete topology and BME-clear state before publishing success.

The fixed snapshot carries a nonzero 64-bit generation and opaque 256-bit
identity generated from checked coreboot RNG calls. This value is a private,
boot-local capability name, not a digest. It is returned unchanged only after
the complete retained policy has been revalidated. Any later mismatch poisons
the authority.

The caller supplies an already canonical MOR clear plan. Every byte of the
handoff page, live table, and table mirror must be contained by an explicit
`ACTIVE_FIRMWARE` exclusion. Each delegated payload DMA arena requires an
explicit `PLATFORM_RESERVED` exclusion. A gap or partial, ambiguous, or
wrong-reason exclusion fails closed. The plan and output are range/alignment
checked, cannot alias, mutable input is snapshotted and rechecked, and output is
published once only after final hardware revalidation.
