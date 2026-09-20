# Bounded TPM2 transport release

The TPM2 transport-release helper provides the last transport-specific step
needed by a pre-OS TPM owner before control passes to an operating system. It
supports the CRB and FIFO/TIS state machines without selecting a device,
requesting a locality, installing a lifecycle provider, or defining platform
policy.

The caller supplies fallible register callbacks, a locality that it already
owns, and finite polling limits. Register offsets are relative to that
locality, so the same helper can be used with memory-mapped, SPI, I2C, or other
platform transports. The helper snapshots this description before the first
callback and performs no allocation. Each polling phase is capped at ten
seconds even if a caller supplies individually valid count and delay values.

`tpm2_transport_quiesce()` moves an owned transport to its specified idle
state. `tpm2_transport_release_locality()` first proves that state and then
relinquishes the locality. `tpm2_transport_quiesce_and_release()` joins those
operations and will not release after an I/O error, invalid ownership state,
or timeout. None of the entry points acquire or seize a locality.

For CRB, quiescing first completes any pending `cmdReady` transition, then
waits for an active command to finish, and only then requests `goIdle`. A
pre-existing `goIdle` request is waited on rather than repeated. Fatal status,
simultaneous request bits, impossible command state, or loss of locality stops
the sequence before any subsequent write.

The code is deliberately dormant: it has no provider instance, global state,
board binding, default policy, provisioning path, SMI hook, or direct hardware
access. A later platform-specific provider may bind its register accessors to
the helper and use the two separate operations as the pre-OS lifecycle's
quiesce and release callbacks.
