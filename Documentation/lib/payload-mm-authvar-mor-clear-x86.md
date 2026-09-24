# x86 MOR clear executor backend

`PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_BACKEND` is a dormant generic x86 mapping,
cache-maintenance, and fence backend for the bounded MOR clear executor. It
installs no boot-state hook, advertises no platform support, allocates no
memory, and never changes MTRRs or PAT configuration.

Before execution, a platform supplies four exact, pairwise-disjoint objects
fully covered by canonical excluded spans: the backend state, the executor
operations table, a
4 KiB-aligned 20 KiB page-table buffer, and a 2 MiB-aligned 2 MiB virtual
aperture. The backend validates the complete plan and exact containment of all
four objects before publishing callbacks. The platform remains responsible for
choosing an aperture whose identity physical range is unused while remapped;
that exact physical range must also be covered by the aperture exclusion
validated by the backend. Canonical adjacent exclusions may be merged; the
backend nevertheless rejects partial coverage or coverage by a cleared span.

Each map operation rebuilds the standard coreboot PAE tables. Those tables
identity-map the 32-bit address space so executing ramstage code and data remain
reachable, then `pae_map_2M_page()` replaces only the aperture mapping. The
executor's 2 MiB physical-window geometry ensures one request cannot cross that
mapping. The returned pointer is always aperture plus page offset, including
physical address zero, and PAE supplies high address bits above 4 GiB.

Mapped bytes are written back and invalidated with `clflush`; the executor's
following fence callback executes `mfence`. Existing MTRR types continue to
apply. Every unmap and every backend failure disables PAE and verifies both
`CR0.PG` and `CR4.PAE` are clear. A mismatch, unsupported `clflush`, callback
mutation, or failed disable terminally poisons the caller-owned backend. Thus a
later payload cannot inherit a successful backend operation with a stale high
mapping.

The private test seam substitutes paging and cache primitives only in host
tests. Production exports only the fixed prepare API and directly uses
`init_pae_pagetables()`, `pae_map_2M_page()`, `paging_disable_pae()`, `clflush`,
and `mfence`.
