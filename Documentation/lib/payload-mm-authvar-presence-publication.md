# Payload-MM authenticated-variable presence publication

`PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION` is the dormant generic boundary
between the fixed presence producer and the coreboot table. It is hidden,
defaults off and is not selected by any platform. It installs no APM route and
provides no platform security proof.

The weak platform-required hook returns false, so even a non-platform test
build of this component is inert: it reserves no memory, constructs no
capability, installs no authority and emits no record. A future platform must
return true only for its proven cold-boot composition. The pre-device exit hook
then registers the producer's aligned bootmem request before device
initialization and before bootmem resolution. A warm or S3 path must remain
unrequested and independently close any retained authority.

The table writer runs immediately after the memory table initializes bootmem
and resolves aligned reservations. For an armed boot it obtains the trusted
platform composition, composes the producer and consumes the one-shot
publication handoff into a local endpoint. It then atomically commits the
publication and allocates the table record only after that commit succeeds. No
callback or fallible operation follows the commit: the exact endpoint is
copied into exactly one record and all local state is scrubbed. Reentry before
the commit terminally poisons the transaction; a contender which loses to an
already committed publication cannot alter or abort it. Any armed-path failure
aborts the producer and stops the boot. A publication-enabled build also stops
if the completed coreboot table exceeds its allocation; the behavior of builds
without this component is unchanged.

This boundary does not supply the protected ramstage-to-SMM seed transfer,
exclusive APM routing, DMA isolation, CPU rendezvous, cold reset, lifecycle or
local-presence proofs. A platform must compose all of them before selecting
publication.
