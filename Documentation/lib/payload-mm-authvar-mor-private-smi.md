# Private MOR completion SMI

`PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI` is a default-off, one-shot bridge from
ramstage's completed memory overwrite to the protected authenticated-variable
executor. It is not an APM command, SMMSTORE operation, coreboot-table record,
or payload interface.

Before bootmem initialization, platform composition registers one aligned
4 KiB `BM_MEM_TABLE` page. During permanent SMM loading it supplies the opaque
reservation handle, trusted cold-boot generation, and fresh receipt and channel
secrets. The loader provisions a signer retained in ramstage and a verifier in
the permanent SMM module parameters. It passes no completion grant at this
point. S3 is ineligible because the receipt authority accepts only an explicit
cold boot.

After bootmem commits the reservation, ramstage resolves the page and supplies
its nested MOR seal channel to the authenticated-variable SMM bootstrap. Once
memory clearing produces the final completion grant, the sender emits the
one-shot reservation receipt, writes the page-bound request, and delivers a
non-APM self-SMI. At the triggering APIC instruction, RBX carries the boot-local
identity, RSI the fixed page address, and RDI a nonzero cookie derived from that
identity, the cold generation, page, owner CPU, and maximum CPU count. These
registers are not inputs to either xAPIC or x2APIC delivery. SMM accepts only
the originating BSP save-state slot and returns the explicit result through
saved RAX.

The handler recognizes the request under the existing global SMM lock before
normal southbridge, northbridge, or CPU SMI dispatch. It consumes the protected
receipt authority, checks the fixed page, generation, capability, channel
identity, originating CPU, zero padding, and callback-stable seal request, then
uses the existing MOR seal receiver and Control-clear transaction. Success,
failure, malformed input, replay, and unused close all wipe the page and
terminally close both sides.

This primitive deliberately does not claim a working platform composition. A
board still has to provide the registered page handle, fresh entropy and cold
generation, invoke the SMM bootstrap at its permanent-load boundary, and call
the sender after its accepted MOR clear path has produced the completion grant.
