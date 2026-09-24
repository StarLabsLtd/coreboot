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

The adapter adds the page tables, aperture, backend state, and operations table
to the canonical live inventory. Page tables and runtime state are active
firmware; the virtual aperture's identity physical range is platform-reserved.
Only then does it prepare the generic backend with the exact returned
addresses. Input mutation, aliases, nonzero or repeated outputs, late or
partial reservations, and S3 preparation fail closed with no published plan or
operations table.

The option is hidden and default-off. A later reviewed cold-boot orchestrator
must register before bootmem initialization and prepare only after successful
initialization and DMA-guard preparation.
