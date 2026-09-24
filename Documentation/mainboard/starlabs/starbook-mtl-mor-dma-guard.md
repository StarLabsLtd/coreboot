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
RNG calls. These values are private, boot-local capability names, not digests.
Repeated preparation is idempotent and revalidates the hardware without
regenerating the token.

`STARLABS_STARBOOK_MTL_MOR_COLD_CLASSIFICATION` retains the authoritative
PM1-derived cold-versus-S3 decision in CAR before FSP-M clears the wake state.
Cold boot allocates an exact 64-byte CBMEM record; S3 recovery only locates the
existing record and does not overwrite its stale bytes. After FSP-M returns,
romstage verifies that VTVC0 protected memory is active and covers the whole
record, clears and reads back BME over the complete segment-zero ECAM space,
rechecks the same protected limit, and only then overwrites the record with
identical sealed copies and a fresh RDRAND generation. Failure is terminal
before ramstage, so an old S3 record cannot become authority.

The dormant ramstage consumer first checks protected-memory coverage, captures
the record, performs a fresh full ECAM quiesce, and rechecks both the protected
limit and every record byte. S3 is always rejected. A valid cold record yields
its exact generation once and is then wiped. An invalid record is wiped only
after its location has been proven protected; a failure before that proof does
not write through an untrusted recovered pointer.

This classifier is intentionally not connected to the current guard. Guard
preparation calls the DMA-live platform `ensure` path, which builds requester
identity from enumerated `struct device` objects. Those objects are unavailable
at `BS_PRE_DEVICE`, before FSP-S, so claiming an immediate classifier-to-guard
transition there would be false. The smallest follow-on prerequisite is a
direct-ECAM early guard preparation path that consumes the retained PCI BME
snapshot identities, followed at bind time by exact equality with the later
enumerated model. Until that exists, no boot hook consumes this record and no
MOR support is claimed. The eventual caller must place the final ECAM quiesce,
one-shot consume, and direct guard preparation contiguously, with no FSP-S or
callback-bearing work between them.

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
