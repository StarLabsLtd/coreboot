# SMM-owned UEFI variables

`SMM_AUTHVAR` selects a protected variable owner for the native CDK2 payload.
The current producer supports Alder Lake/Raptor Lake with fixed SMM BIOS write
protection. S4 requires the shared fixed communication mailbox and the payload's
stable allocation bins; otherwise hibernation must be disabled. Ordinary
payload-owned SMMSTORE remains a separate composition.

Confirmed Setup actions use the owner's finite-action endpoint and boot-phase
closure. Authenticated firmware updates use the separately selected typed capsule
transport, not raw SMMSTORE writes or the variable mailbox. Selecting the variable
owner alone does not provide a firmware-update resource.

The owner, policy registry, crypto workspace, media buffers and complete request
and response snapshots reside in the installed SMM module. Its allocation participates
in the existing module loader's size and overlap checks. The selected producer
requests 16 MiB TSEG through the existing FSP setting; IED, stage cache and OPAL
reservations retain their normal ownership.
The existing per-CPU SMM stack setting becomes 16 KiB for this owner: the
authenticated-update call chain includes a roughly 4.5 KiB X.509 verifier frame
in addition to its callers. A smaller explicit stack setting is rejected.

Only the owner allocation occupies a retained NOLOAD tail, outside the transient
handler BSS. The loader clears it on cold boot and preserves it on S3; normal
SMM locks, CPU maps and driver initialization are rebuilt. S3 requires coreboot's
TSEG stage cache so the retained callbacks keep their original image/placement.
Each entry checks the original communication geometry and reattaches the SPI
backend after the hardware gate. Missing retained state cannot reopen a fresh
owner on resume. No second owner snapshot or generic rmodule ABI is introduced.

Variable-managed and firmware-fixed Secure Boot use the same owner. Fixed mode
links CDK2's plain public-asset descriptor into SMM rather than copying a key
list into the producer. PK/KEK/db and policy controls remain locked, while dbx
accepts authenticated updates that retain the firmware baseline. Incompatible
or unprovisioned stores are rejected. A durable startup dbx maintenance update
requires a cold reset before the endpoint is published. The separate offline
factory-image target remains an initial-installation operation, not migration.

Before publishing the endpoint, ramstage synchronously initializes the installed
service. Every service entry requires current CPU rendezvous and SMRR readback,
locked chipset TSEG boundaries matching the installed region, locked SMM code
access checking, and fixed BIOS write protection. Failure does not publish a
replacement payload-owned service. Broad board/SoC finalization remains at its
existing boot state.

The service also requires the SPI controller's writable BAR to match coreboot's
fixed SoC assignment, with memory decoding enabled, before touching controller
MMIO. BIOS interface and flash-configuration locks do not freeze that BAR.

The loader-owned SMMSTORE communication buffer is untrusted. Each operation
copies its complete advertised size into SMRAM before parsing. No request pointer
or shared length is passed to the owner or flash driver. Policy checking precedes
mutation, including deletion. Runtime transition seals registration, and policy
targets and predicates remain classified as protected across deletion. Ordinary
volatile values use the owner's private banks, never flash. Public image-policy
snapshots are boot-services-only; they do not attest caller origin or protect
payload memory against DMA.

The media adapter uses the compiled SMMSTORE FMAP extent, not mutable CBMEM FMAP
metadata. It reuses authenticated-variable recovery and durable commit/readback.
Raw SMMSTORE write, clear and full-flash commands are denied in this composition.
The existing read-only store and typed SRK operations remain separate consumers.

The endpoint uses one block-sized message, at most 64 KiB, with a 4096-byte name
slot and a 144-byte header. QueryVariableInfo caps its per-variable size at the
transport limit. Larger retained variables or combined public key snapshots can
exceed that limit; a required size is reported, not a truncated successful read.
This is a composition limit, not complete EDK2 capacity parity.
