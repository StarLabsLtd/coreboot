# Pre-OS TPM lifecycle

The dormant pre-OS TPM lifecycle is an allocation-free ownership state machine.
It is a dependency for a future TPM provider; it does not select, probe or
configure hardware and has no board default.

An explicit provider supplies four synchronous operations by value:

1. `begin` establishes exclusive pre-OS transport ownership;
2. `transmit` completes exactly one command with nothing left in flight;
3. `quiesce` proves the command engine and shared buffers are idle; and
4. `release` relinquishes locality, bus and transport ownership.

The provider context is copied into the lifecycle object. Installation begins
exclusive transport use. Logical consumers then acquire the sole owner token,
transmit through the checked wrapper, and end ownership. Tokens contain an
object identity and monotonically increasing generation to catch stale,
cross-object and reentrant use; they are not cryptographic capabilities.
Generation wrap, nested use, a wrong token, provider failure or corrupted state
permanently poisons the lifecycle. A failed lifecycle never permits OS access.

The state, revision guard and complement are published in one naturally aligned,
lock-free atomic control word. Mutators claim each transition with one compare
and exchange and never spin. A concurrent or reentrant caller sets an orthogonal
poison bit with one atomic operation and returns without touching provider
context. The holder keeps context intact until its callback returns, observes
that poison when its final transition fails, clears its outputs and seals the
lifecycle. Callback return checks also revalidate the owner token, generation,
context size and provider identity. A completed handoff is terminal-dominant,
so a late caller cannot revoke permission already granted to the next owner.
The build has a compile-time lock-free assertion and currently links the
primitive only on x86. This preserves existing RISC-V TPM2 builds whose ISA
profiles may omit atomic instructions. Extending the API to another
architecture requires proving its aligned 32-bit operations are lock-free.

Handoff is legal only with no logical owner. It calls `quiesce` and `release`
once, clears all callable provider hooks and context, and makes firmware
commands permanently unavailable. Only a successful terminal handoff makes
`tpm_pre_os_lifecycle_os_access_allowed()` true. Handoff deliberately sends no
`TPM2_Startup` or `TPM2_Shutdown`; the next owner receives the already
initialized TPM.

`tpm_pre_os_lifecycle_state()` is an atomic control-state diagnostic; it does
not inspect separately held provider metadata. Callers must use
`tpm_pre_os_lifecycle_os_access_allowed()` as the fail-closed authorization
predicate for transfer to the next owner.

## Deliberate integration boundary

This patch installs no provider and creates no lifecycle object, so it cannot
change hardware. Existing TSS code still exposes and calls its raw transport.
A production follow-up must make that pointer private and route every TPM 1.2,
TPM 2.0 and policy command through one lifecycle instance. Mixing checked and
raw calls is not exclusive access and must not be enabled.

coreboot and a payload have separate address spaces, so static state is not an
inter-stage lock. Once a provider exists, coreboot must explicitly hand off
after all payload loading, measurement and boot-state entry callbacks, directly
before `arch_bootstate_coreboot_exit()` and `payload_run()`. The S3 path needs
the same explicit release directly before OS resume. Boot-state callback order
is not an ownership mechanism. A failed release must abort transfer and must
not expose a TPM ACPI or device-tree interface.

The payload must independently acquire its transport at entry and retain it
until the actual OS transfer. An EFI `StartImage` call is not that boundary:
the image can use TCG2 services and can return. A future CDK2 integration must
therefore release at its final ExitBootServices/OS-transfer path.

No lifecycle object or TPM transport may be compiled into SMM. An SMM copy has
independent static state and cannot arbitrate with coreboot, a payload or an
OS. Capsule authentication and TPM rollback-state work must complete pre-OS;
later SMM flash work may consume only a bounded grant already copied into
SMRAM, not revisit the TPM.

The default-off [capsule rollback-anchor grant](capsule-tpm-anchor-grant.md)
defines the pointer-free one-shot handoff shape for that future split
transaction. The dormant owner journal supplies the reset-safe prepared and
exact reconciliation phases, but no platform selects them. The tree still does
not supply the signed authorization or TPM-owner producer required to advance
the provisioned index and install that grant safely.

## Provider requirements

Discrete FIFO/MMIO, SPI and I2C providers must finish or abort to
`COMMAND_READY`, relinquish the active locality, and wait boundedly for the
active bit to clear. CRB providers must require `START` clear, request
`GO_IDLE`, wait for idle, then relinquish locality with bounded validation.
They must never expose a half-finished command buffer.

Depthcharge's LPC and I2C cleanup-on-handoff paths demonstrate the locality
release pattern. Its Google SPI cleanup is incomplete and is not evidence of a
safe release path. Base-address publication alone is also insufficient for
SPI or I2C transports.

PR98 intentionally supplies none of those hardware adapters, no runtime SMI,
no NV index or policy, no event-log ABI, no payload table and no implicit
provider. Host tests use fake callbacks only and make no hardware mutation.
