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

After every other coreboot-table builder has finished, the table writer obtains
the trusted platform composition and prepares the producer. It validates the
exclusive table bound twice, including the deferred size of the current last
record, the final endpoint, pointer arithmetic and the 32-bit table counters.
Insufficient capacity aborts before composition, table mutation or authority
commit. The writer snapshots and reserves the exact final record slot before
consuming the producer's one-shot commit handoff.

The handoff returns the endpoint and an address-bound, private-nonce receipt
while retaining the sealed platform fail-stop policy. No callback or ordinary
fallible work follows that commit. The writer verifies that the reserved header
and complete destination span did not change, copies the fixed endpoint, moves
its own state exactly to published, and completes the receipt. Any ambiguity in
that interval invokes the retained platform-wide fail-stop; a returned
pre-commit failure restores the header and destination bytes exactly. The
endpoint is therefore the final record exactly once, followed immediately by
the table checksum finalizer. The receipt is correlation for trusted internal
code, not a public capability, and every local copy is scrubbed.

Reentry before exclusive finalization ownership terminally poisons the
transaction. Once the finalizer owns it, a losing contender cannot alter or
abort it. The sole table owner performs no post-commit callback and is the only
receipt closer. Any armed-path failure aborts the producer and stops the boot.
Builds without this component retain their existing table order and object
code.

This boundary does not supply the protected ramstage-to-SMM seed transfer,
exclusive APM routing, DMA isolation, CPU rendezvous, cold reset, lifecycle or
local-presence proofs. A platform must compose all of them before selecting
publication.
