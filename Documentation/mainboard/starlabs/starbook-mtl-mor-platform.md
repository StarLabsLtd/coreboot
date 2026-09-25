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
reservation. After bootmem resolution, the provider uses the existing MTL x86
binding, then requires the private boundary to attest its loader-owned arena,
transport-page receipt, bootstrap installation, and live chipset SPI-write
restriction before it exposes the executor operations. Completion and close
can only cross that typed boundary.

The private SMI boundary is deliberately an injection point in this change.
The accepted tree does not yet contain a frozen platform binding that can
install the loader receipt, verify the bootmem transport receipt in protected
SMM, prove the chipset write restriction, and perform the terminal typed
completion or close. Until that implementation is reviewed and supplies all
five callbacks, the weak boundary rejects composition and the boot coordinator
halts. No raw SMMSTORE or generic APM fallback is introduced.
