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

Preparation is deliberately independent of a MOR clear plan. It establishes
and twice observes the hardware, then retains an exact snapshot with a nonzero
64-bit generation and opaque 256-bit identity generated from checked coreboot
RNG calls. This value is a private, boot-local capability name, not a digest.
Repeated preparation is idempotent and revalidates the hardware without
regenerating the token.

Binding is a separate terminal transition. The caller supplies the prepared
snapshot and an already canonical MOR clear plan whose inventory generation
and identity exactly match the retained guard token. Every byte of the
handoff page, live table, and table mirror must be contained by an explicit
`ACTIVE_FIRMWARE` exclusion. Each delegated payload DMA arena requires an
explicit `PLATFORM_RESERVED` exclusion. A gap or partial, ambiguous, or
wrong-reason exclusion fails closed. Binding reobserves the hardware and
returns both the exact bound snapshot and the generic DMA token used by the
clear executor. Repeated binding accepts only the identical retained plan.

The authority follows `EMPTY -> PREPARED -> BOUND`; any operational failure
after a transition begins invokes the platform poison callback and enters the
terminal `POISONED` state. Inputs and outputs are range/alignment checked and
cannot alias. Mutable inputs and callback operations are snapshotted and
rechecked, outputs remain zero until final publication, and no path installs a
boot-state hook or advertises MOR support.
