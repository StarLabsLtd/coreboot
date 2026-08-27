# Atomic CFR settings service

The CFR settings service lets a payload or operating system read and update a
small firmware-owned allowlist of numeric CFR options. It does not expose
SMMSTORE offsets, arbitrary EFI GUIDs, variable names, or caller-provided
pointers to SMM.

The service is disabled by default. `DRIVERS_OPTION_CFR_SMM` requires x86 SMM,
SMMSTORE, the coreboot EFI variable backend, and a supported SMI dispatcher. A
board must provide a policy and compile the same policy objects into ramstage
and SMM. The weak default policy is empty and fails closed.

## Published interface

An allowlisted boolean, enum, or number receives a
`CFR_TAG_OPTION_ACCESS` child record containing a nonzero, stable token and
READ or READ | WRITE permissions. The token is an identifier, not a secret or
an authentication credential. Consumers must not infer write permission from
`CFR_OPTFLAG_RUNTIME`.

`LB_TAG_CFR_SETTINGS` publishes the physical address and exact size of one
64-byte, coreboot-allocated CBMEM mailbox and the APM command used to enter
SMM. SMM obtains that address from protected runtime parameters; it never
accepts a communication-buffer pointer from the caller.

Mailbox version 1 contains a nonzero 64-bit sequence, command, status, token,
expected old value, requested value, current value, response flags, and five
reserved words. GET and SET are the only commands. Exact retries return the
cached response without repeating a transaction. Sequence ordering uses
serial-number arithmetic, so the stream can wrap while rejecting stale
requests. Malformed requests do not advance the replay watermark.

## Board policy

`mainboard_cfr_settings_policy()` maps tokens to exact `sm_object` pointers and
READ, WRITE, and optional runtime-apply policy bits.
`mainboard_cfr_settings_forms()` returns full `SM_OBJ_FORM` roots for the same
tree that the board serializes into its CFR table.

Policy validation rejects malformed metadata, duplicate tokens or options,
unsupported option types, writable non-runtime settings, missing dependency
controllers, inherited read-only flags, dependency cycles, and trees exceeding
the documented implementation limits. Every direct, inherited, and transitive
dependency is re-read and enforced by SMM before a SET.

SMM-managed options use the default in their policy `sm_object`. A registered
CFR default override is ignored for such an option so the published table and
SMM fallback cannot disagree. Boards should put the intended default directly
in the shared object.

SET uses compare-and-set semantics and then:

1. validates the value, dependencies, and optional board policy;
2. persists and reads back the EFI-compatible coreboot option;
3. reports that a reboot is required, or invokes and verifies a runtime hook;
4. restores and verifies both persistence and runtime state after a failure.

A failed recovery latches the write path as indeterminate until cold boot.
Diagnostic GET requests remain available.

## Variable-store initialization

The first GET may initialize the EFI firmware-volume and variable-store
headers when SMMSTORE is entirely erased. It does not persist CFR defaults.
Initialization can also complete an interrupted header write when every
programmed bit is consistent with the expected header and all following bytes
remain erased. Arbitrary malformed or nonblank stores are rejected without an
erase or repair attempt.

This is not power-loss-atomic variable storage. Reset or power loss while
appending a variable can still require platform recovery, and an authorized
operating system can consume store endurance or space.

## Platform validation

An available common SMI dispatcher only establishes that the service can be
wired on that platform family. Each board still has to validate its policy,
flash write-protection transitions, persistence, S3 behavior, and any runtime
apply and verification callbacks on hardware.
