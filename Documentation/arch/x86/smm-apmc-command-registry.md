# SMM APMC command manifest

`SMM_APMC_COMMAND_REGISTRY` is a hidden, default-off x86 build option. It
builds a read-only command manifest and two-phase selection contract, but does
not install a dispatcher, select a board, write `APM_CNT`, or change an
existing SMI route.

The manifest separates three facts:

* the reserved byte namespace, including disabled and currently unused
  definitions;
* an owner enabled by the current configuration; and
* a route capability selected by the Kconfig family that builds the audited
  dispatcher case.

`smm_apmc_command_select()` first returns `UNKNOWN`, `CONSUMED_REJECT`, or an
enabled descriptor naming the exact configured owner. Only the enabled result
permits the trusted dispatcher to invoke that owner. Afterwards
`smm_apmc_command_finish()` takes the independently frozen command, snapshots
and clears the caller's descriptor, re-derives the configured claim, and
accepts only an exact descriptor and a `HANDLED` owner outcome. This prevents
trusted dispatcher code from treating an outcome as authoritative before
selecting its configured owner.

The descriptor is copyable correlation data, not a capability. Clearing the
passed copy does not invalidate other copies or defend against malicious SMM
code. No caller exists in this slice; a later live integration must bind the
independently frozen command to protected invocation evidence.

Reserved bytes remain consumed even when their service is disabled. An
enabled owner must have exactly one dispatcher binding or compilation fails.
Two enabled owners using the same byte produce duplicate generated switch
cases and also fail compilation. Only a byte outside the reserved namespace
is classified as unknown and eligible to fall through. A disabled, unready,
malformed, rejected, or failed recognized command is classified as consumed.
This contract remains dormant until a later patch composes it into every
relevant dispatcher; current handlers do not enforce it.

Every enabled service, including ACPI enable, ACPI disable, and finalize, has
one exclusive semantic owner. Optional lifecycle observers must have their
own explicit ordered manifest. No observer is attested by this slice.
Arbitrary mainboard hooks are not treated as observers and remain an
audit/migration blocker for system-wide enforcement.

Route capabilities are selected by source families containing the respective
case; service Kconfig options do not assert their own routes. The complete
composition is attested only for Q35 and the audited StarLabs Intel-common
handler.
The registry cannot be enabled on the uncatalogued legacy southbridge, AMD,
or arbitrary-mainboard compositions. Their constants remain reserved, but no
claim that their current dispatch is complete is made. Kconfig constrains the
option to an attested composition and the implementation repeats that
requirement as a compile-time assertion, so a future improper `select` also
fails the build.

## Audit results

The shared constants in `cpu/x86/smm.h` reserve bytes even where no handler
was found, notably `APM_CNT_NOOP_SMI` (`0x00`) and `APM_CNT_MBI_UPDATE`
(`0xeb`). Common Intel, AMD, legacy, and QEMU handler families implement
overlapping semantic services under mutually exclusive platform
configurations.

Two separate services currently define `0xe8`:

* the capsule broker transport; and
* the payload SPI console.

QEMU Q35 dispatches the SPI console command. Intel common-block SMM can build
the SPI console feature but has no matching dispatch case, so that
configuration is an enabled orphan and the manifest build rejects it. The
capsule broker defines `0xe8` but currently has no platform dispatcher
binding, so enabling it with this manifest is rejected. Enabling both owners
also exposes the numeric collision at compile time.

StarLabs dispatches its EFI-option command at `0xe2`. Acer VN7-572G dispatches
its board command at `0xdd`. Raw SMMSTORE owns `0xed`; its full-flash operation
is an AH subcommand, not another APMC owner. The authenticated-variable
memory-overwrite-request handoff has no APMC command.

The host harness pins those platform-local constants to their source
definitions, accounts for every shared `APM_CNT_*` definition, tests valid
Q35, StarLabs, and Acer profiles, and requires orphan and collision profiles
to fail compilation. Future dispatcher integration must additionally provide
evidence for the unique initiating save-state node. This manifest deliberately
does not infer that from the SMM lock winner or add a weak initiator API.

The profile gate builds the registry, handler, and stub objects for GLK, ADL,
MTL, and Q35, and completes a clean-tree Q35 SMM link. A complete MTL SMM link
remains in platform validation because its board configuration names external
StarLabs FSP headers which are not part of the coreboot checkout.
