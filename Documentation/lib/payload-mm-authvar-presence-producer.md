# Payload-MM authenticated-variable presence producer

`PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER` is a dormant ramstage composition
boundary for the fixed `ENTER_SETUP_MODE` authority. No platform selects it.
It installs no APM route and contains no coreboot-table writer.

Before bootmem initialization, `payload_mm_authvar_presence_producer_reserve()`
registers one page-sized, page-aligned, below-4-GiB `BM_MEM_RESERVED` backing
range. The public endpoint exposes only the exact 80-byte mailbox at its start.
The DMA proof covers the complete backing page; the lifecycle proof gates the
mailbox and endpoint publication. Any failure after the reservation resolves
scrubs that complete page. A failure before resolution cannot address or scrub
the page, but no capability has been generated or written and no endpoint is
published. The reservation itself cannot be returned.

After bootmem initialization, a future platform supplies one immutable policy
and copied context. The policy must prove a cold boot, transfer the private seed
into protected SMM and install the authority, and independently prove DMA
protection, active CPU rendezvous, cold-reset readiness, lifecycle closure and
the complete platform route/local-presence composition. The install callback
must copy and consume the seed using a protected SMM-loader mechanism; retaining
the ramstage pointer is invalid. Failure is terminal for the boot and closes any
possibly installed authority, scrubs resolved backing and exposes no record.
The producer becomes irreversibly terminal and returns an error; the future
platform caller must turn that error into a boot stop. This dormant boundary
does not yet install that caller.

The six public flag bits are admitted as follows:

* `COREBOOT_SMM_OWNER`: the protected authority install completed.
* `FIXED_COMMUNICATION`: page backing resolved for the exact mailbox window.
* `DMA_PROTECTED`: the injected range proof accepted the complete backing page.
* `CPU_RENDEZVOUS`: the injected global-rendezvous proof accepted.
* `ONE_SHOT_CAPABILITY`: the boot-local capability was generated once and
  transferred to the one-shot authority.
* `LIFECYCLE_SEALED`: the injected lifecycle proof covers pre-external-image,
  READY_TO_BOOT/ENTER_RUNTIME and S3/platform restriction hooks.

Reset readiness and the platform proof are mandatory even though they do not
have separate public flag bits. All proofs are rerun after mailbox provisioning
and immediately before the explicit publication handoff. Only
`payload_mm_authvar_presence_producer_publication_take()` can release the exact
64-byte record, once. The handoff clears the producer's copied policy, context,
reservation handle and internal endpoint; it does not itself add tag `0x56` to
a coreboot table.

Generation, request ID and the opaque 256-bit capability come from six
`get_random_number_64()` calls. The capability is copied only to protected SMM
and the DMA-protected mailbox, never to the public record. Warm boot and S3
resume must fail the injected cold-boot proof, so they cannot re-mint authority.

This boundary does not make a platform security claim by itself. A platform
must atomically compose and test every callback, the protected SMM transfer,
the exclusive trigger route, lifecycle callers and final table writer before it
may select this option or publish the endpoint.
