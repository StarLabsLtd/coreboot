# StarBook MTL linear MOR platform provider

`STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER` is a hidden, default-off
composition prerequisite. No board selects it and it makes no support claim.

The pre-FSP-S board hook now treats an early DMA preparation failure as fatal.
It consumes the protected romstage classification exactly once and retains
either a cold generation plus early guard, or an S3 classification. The linear
coordinator receives generation zero for S3 and closes without probing or
registering any clear reservations.

The SMM loader seed uses the retained runtime generation and fresh ramstage
entropy. An asserted MOR request conditionally registers the existing MTL PAE
page-table and aperture reservations plus the private boundary's transport
reservation. Early discovery is a hard dependency: it seeds the owner and
registers every reservation before permanent SMM loading can consume the arena
seed. After bootmem resolution, the provider first requires the private
boundary to return its authenticated transport receipt as an exact aligned
4 KiB `BM_MEM_TABLE` range. Only then does it construct the MTL x86 binding,
which excludes that page from clearing as active firmware. Completion and
close can only cross the typed boundary.

The private SMI boundary is deliberately an injection point in this change.
The accepted tree does not yet contain the protected implementation that
authenticates the loader and bootmem receipts, installs the SMM bootstrap,
proves the chipset write restriction, and performs terminal completion or
close. This change deliberately stops before installing that implementation:
until a later reviewed slice supplies all five callbacks, the weak boundary
rejects composition and the boot coordinator halts. No endpoint, raw SMMSTORE,
or generic APM fallback is introduced.
