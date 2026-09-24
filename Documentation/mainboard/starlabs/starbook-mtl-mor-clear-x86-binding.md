# StarBook MTL MOR x86 backend binding

`STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_BINDING` builds a dormant adapter between
the StarBook Meteor Lake live-DRAM inventory and the generic x86 MOR clear
backend. It installs no boot hook, clears no memory, publishes no endpoint, and
makes no MOR support claim.

Before bootmem initialization, the adapter registers an exact 20 KiB,
4 KiB-aligned `BM_MEM_TABLE` range for PAE page tables and an exact 2 MiB,
2 MiB-aligned `BM_MEM_RESERVED` aperture. Both have an explicit exclusive
4 GiB limit. After initialization, it queries rather than moves those exact
ranges and rejects changed handles, wrong sizes, tags, alignments, limits, or
overlap.

The adapter adds the page tables, aperture, returned plan, and its complete
binding state to the canonical live inventory. Page tables, plan, and binding
are active firmware; the virtual aperture's identity physical range is
platform-reserved. Only then does it prepare the generic backend with the exact
returned addresses. Input mutation, aliases, nonzero or repeated outputs, late
or partial reservations, and S3 preparation fail closed with no published plan
or operations table.

The binding fills the executor's DMA-snapshot and live-inventory operations
from the already prepared Meteor Lake guard. It does not rediscover hardware.
Each callback repeats the guard's retained early-ECAM, enumerated-model,
translation-engine, table, and Bus Master Enable equality checks. The inventory
callback also recomposes bootmem with the exact retained overlays and requires
byte equality with the canonical plan. The resulting DMA token is the exact
generation and identity returned when that plan was bound.

Twin-sealed private state binds the owner address, prepared and bound guard
snapshots, DMA token, plan, overlays, x86 backend, and complete operations
table. Callback-local copies detect mutation across every hardware or bootmem
callback. A monotonic `inventory -> DMA -> DMA -> inventory` lifecycle matches
the generic executor and makes stale, malformed, aliased, out-of-order, or
replayed calls terminal. Its owner, cold generation, and sequence state live in
file-private ramstage memory outside the caller-owned binding, and the sequence
advances before external callback work. Restoring an earlier binding image
therefore cannot restore authority after completion or failure. Callback
contexts must cover the complete binding, match that private owner exactly,
and pass alignment and overflow checks before any enclosing state is read.
Repeated guard binding only revalidates the existing authority; it neither
generates a new identity nor re-enables a requester.

The option is hidden and default-off. A later reviewed cold-boot orchestrator
must register before bootmem initialization and prepare only after successful
initialization and DMA-guard preparation.
