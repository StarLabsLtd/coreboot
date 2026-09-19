# Payload-MM authenticated-variable boundary

`PAYLOAD_MM_AUTHVAR_CONTRACT` is a default-off prerequisite for a native CDK2
authenticated-variable service running inside a coreboot-owned SMM environment.
It is deliberately smaller than the Payload-MM loader and MM dispatcher proposed
in coreboot Gerrit changes 89031 and 89151.

The contract accepts platform facts only when coreboot owns SMM entry, SPI writes
are SMM-only, the complete variable-store region is owned by that SMM path, the
communication buffer is bounded and outside SMRAM, and no raw/full-flash
transport remains available. The store must be a proper subset of boot media,
use power-of-two geometry and contain at least three erase blocks.

The ramstage builder validates the proposed contract. The separate SMM runtime
installs one private copy in its own static storage, exactly once. A mandatory
platform hook proves that the complete static authority object is protected
SMM storage; outputs that alias that object are rejected. Runtime code
first proves that the complete request descriptor is in the authoritative
communication range, snapshots it once, validates that snapshot, then copies
the opaque message into caller-provided SMRAM storage. Native CDK2 must parse
only those trusted copies and never reread shared RAM.

The single installation attempt is consumed before inspecting its input. A
malformed initialization therefore fails closed and cannot reopen authority
installation through a later SMI.

The request contains no flash address, block number, erase command or write
command. Authenticated-variable parsing, signature and timestamp checks, replay
protection, append/delete policy, durable transaction state and final mutation
authorization belong to native CDK2 code. The contract does not claim that any
of those policies exist.

## Source comparison

The local Gerrit-derived Payload-MM series demonstrates SMRAM reservation,
one-attempt loader hardening, Intel SMI entry and SPI geometry, but also imports
a broad loader/dispatcher lifecycle. EDK2 26.09 `BlSmmCpuPayloadMm` similarly
provides an MM CPU environment and rejects forbidden communication addresses.
Neither is copied here: coreboot retains its silicon SMM entry and this ABI only
admits a previously reserved communication range and variable-store geometry.

## Production gates

No producer, coreboot-table record, SMI command or dispatcher is installed. The
runtime authority is compiled only into the SMM module; ramstage state is never
treated as runtime authority.
`Q35_PAYLOAD_MM_AUTHVAR_TEST_PROOF` only compiles this library for the QEMU Q35
proof build; it does not weaken these production gates.
Before selection, a platform must provide all of the following through static,
platform-owned verification hooks, never caller-supplied trust booleans:

* an SMRAM reservation and coreboot-owned SMI entry that survive S3;
* a reserved runtime communication range checked against every SMRAM and
  protected-memory range before copying a request snapshot;
* an SMM-only SPI backend whose descriptor/FMAP policy proves exact ownership
  of the variable-store region and keeps BIOS/full-flash access unreachable;
* a matched native CDK2 handler that validates authenticated-variable policy and
  closes registration before OS handoff;
* hostile QEMU evidence followed by Intel and AMD hardware validation.

The generic SMMSTORE raw read/write/clear interface and its capsule full-flash
modifier are not a backend for this contract. Enabling this library alone does
not provide authenticated variables.

Host evidence is provided by:

```
tests/lib/payload_mm_authvar_test.sh
```
