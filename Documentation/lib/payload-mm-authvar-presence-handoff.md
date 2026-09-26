# Payload-MM authenticated-variable presence handoff

`PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF` builds a dormant, private, two-phase
transport for one presence-authority seed. It is hidden, defaults off, has no
board selection, and installs no SMI cause, APM command, public capability, or
coreboot-table record.

Before bootmem initialization, the sender registers a dedicated aligned 4 KiB
`BM_MEM_RESERVED` transfer page. Before a permanent SMM handler can be accepted,
a future loader must supply the already-registered presence-mailbox handle,
independent channel capability and generation, two independent receipt secrets,
the BSP owner, and the loader's actual active CPU count. The protected receiver
slot receives separate one-shot verifiers for the mailbox and transfer page.
Every safely addressable, nonalias loader seed is consumed and scrubbed once a
provisioning call can identify it, including invalid-slot, replay and wrong-state
returns. A null, overflowed or aliased pointer is rejected without access or
scrub. A protected cold-boot proof is mandatory; its weak default rejects
provisioning, so S3 and warm paths cannot provision or reuse the channel.

After bootmem resolves both handles, the sender requires exact 4 KiB reserved
results and emits two exact-`BM_MEM_RESERVED` receipts. The private request page
contains the endpoint seed and binds both ranges, the private identity and
cookie, channel generation and capability, BSP, and actual CPU count. A trusted
platform callback must deliver the immutable descriptor through a private BSP
self-SMI mechanism. The weak callback fails closed. Identity and cookie are
internal descriptors, never selectors or public routes.

The receiver first claims its private receiver phase and then its protected
slot. The sole owner performs all close and scrub operations; abort and stale
receivers cannot write caller results or slot state. The slot remains in a
nonterminal `CLOSING` state until both verifiers and all metadata are scrubbed;
the release-published terminal state is the owner's final slot access. The
receiver accepts only its protected loader slot and requires a protected
platform DRAM/non-MMIO proof for the descriptor, result, and entire transfer
page before dereferencing them. It claims the request once, authenticates the
transfer page before scrubbing it, authenticates the mailbox independently,
rechecks immutable snapshots and all bounds, then invokes the sole protected
presence-authority install callback. Every attempt terminally closes both
verifiers and the channel. Authenticated requests scrub the complete transfer
page; an unauthenticated attacker-selected range is never written. The public
mailbox is never scrubbed by this transport.

## Deliberate integration blockers

This primitive is not wired into the producer, publication path, generic SMM
handler, SMM loader, or `smm_runtime`. Production composition first needs one
transaction that defines authenticated `ABORT` and `COMMIT`, authority rollback,
mailbox initialization, and route arming so an authority cannot become open
before publication commits. The producer must prove immutable cold-boot state,
full-page DMA protection for both mailbox and transfer pages, and a real
all-active-CPU rendezvous before calling the sender, then repeat those proofs
before publication. A platform must also provide a private-cause discriminator,
protected slot accessor and request-range provenance proof. The SMM global lock
is not an all-CPU rendezvous. Until those pieces are composed and tested, this
code makes no platform support or security claim.
