# Payload-MM authenticated-variable presence authority

`PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY` composes the dormant revision 1
presence ABI with a protected, boot-local SMM authority. No platform selects
it, publishes its table record, or installs its APM route in this slice.

Installation is one-shot. A protected provisioning callback creates the opaque
256-bit capability for the endpoint's nonzero boot generation. The authority
retains two sealed protected copies, rejects the all-zero sentinel, and never
places either copy in a coreboot table. A future producer is responsible for
protected capability delivery; this slice provides no CDK2 or UI producer.
Callback context bytes are copied into dual authority-owned SMM buffers; the
authority never retains the installer's context pointer. Context equality is
checked around callbacks, so a callback cannot turn mutable external state or
an in-place context mutation into changed provisioning, proof, or reset
semantics.

The endpoint remains open only during its boot-only presence window.
`LB_AUTHVAR_PRESENCE_LIFECYCLE_SEALED` asserts that this mandatory seal is
implemented, not that a newly published endpoint is already closed. Revision 1
has no public close command. Valid authenticated-variable `READY_TO_BOOT` and
`ENTER_RUNTIME` lifecycle transactions therefore close and scrub this channel
under the existing global executor gate. The private close entry is available
for a future paired producer's mandatory pre-external-image, S3, and platform
restriction hooks; this slice installs none of those platform hooks. No
platform may publish the endpoint or assert `LIFECYCLE_SEALED` until those
concrete hooks are composed and tested. Closure itself is irreversible.

## Dispatch and consumption

The exclusive APM route accepts only the endpoint's fixed port and value. A
wrong port or value is non-consuming. An exact trigger atomically changes the
authority from open to attempted and scrubs both retained capability copies
before reading shared mailbox bytes or invoking DMA and rendezvous proofs. The
claim is terminal: malformed frames, incorrect capabilities, proof failures,
busy or media failures, and reset failures cannot reopen the endpoint. An
incorrect capability receives only `SECURITY_VIOLATION`; malformed or unsafe
mailbox state is not trusted enough for a completion write. The close and claim
operations race on the same state transition, so exactly one can win.

After the claim, SMM rechecks the sealed policy, exact DMA protection for the
fixed 80-byte mailbox, and an active all-CPU rendezvous. It snapshots and
structurally validates the request, then rechecks both proofs. It revalidates
both proofs again immediately before every completion write, including after
the arbitrarily long executor transaction. Lost protection suppresses the
completion write. Concurrent or later entries cannot invoke the executor
again.

The sole mutation sink is
`payload_mm_authvar_executor_enter_setup_mode()`. It acquires the executor's
global authenticated-variable single-flight gate and calls the fixed internal
presence coordinator. There is no caller-provided variable name, GUID,
attributes, data, Auth2 envelope, policy callback, or generic `SetVariable`
route.

The fixed coordinator atomically deletes `PK` and, when present,
`SecureBootEnable`, then verifies the complete committed candidate and its mode
projection before success. `CustomMode`, `KEK`, `db`, `dbx`, and `dbt` are not
mutations in the fixed bundle. If the protected store is already in a fully
consistent SetupMode state—both `PK` and `SecureBootEnable` absent—the action is
an idempotent success with no media write. An orphaned `SecureBootEnable` is an
invariant failure, never an idempotent success. Runtime or ready-to-boot entry
is write protected even if a lifecycle caller failed to seal the channel.

Every trusted completion echoes the entire request identity and capability,
normalizes internal results to the ABI's closed status set, and publishes
completion last with release ordering. Success first publishes `SUCCESS`, then
immediately invokes the sealed cold-reset callback. If that callback returns,
the authority poisons itself and production fail-stops; it cannot return to its
caller or continue boot. Host tests alone use a controlled return escape after
poisoning. The executor separately reports whether a mutation was attempted and
may already be durable. That indication forces reset/fail-stop even if late
verification or media-lease release changes the visible result to an error.
A valid already-SetupMode no-op is also successful and always resets. Reset
uses a pre-proof private copy of the sealed context, so a proof callback cannot
alter reset semantics.

Provisioning is one-shot but transactional at the authority boundary. If the
provision callback fails, returns an all-zero capability, or mutates sealed
policy/context, the authority scrubs both its local capability and the complete
fixed mailbox before poisoning itself.

This composition still makes no platform security claim. Its callbacks must be
bound to real protected provisioning, DMA isolation, CPU rendezvous, exclusive
SMI routing, lifecycle ordering, and a non-returning cold reset before a
platform may publish the endpoint.
