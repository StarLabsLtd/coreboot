# Fixed EFI variable service

This interface runs an EFI authenticated-variable implementation from a fixed,
coreboot-owned SMRAM region. The payload can submit requests but cannot register
SMM code or choose an executable address.

## Service image

The coreboot build places the IA32 `FIXEDMMFV.Fv` firmware volume in CBFS as
`efi_variable_service_fv`. Ramstage copies that fixed volume into the reserved
EFI variable-service SMRAM region, loads and relocates its IA32 Standalone MM
core, and supplies the PI HOBs required by the selected drivers. The FV, loaded
image, HOB list and remaining MM heap all stay in that region.

Initialization runs once in SMM during platform finalization. A private initialization
HOB supplies a one-shot callback used by the fixed EDK2 bridge to publish the
standard MM configuration protocol. StandaloneMmCore registers its internal
runtime dispatcher through that protocol. coreboot accepts one registration,
checks that the entry lies in the loaded MM-core image, stores it in SMM-only
state and removes the callback. Neither the callback nor either entrypoint is
published to the payload.

The variable-store and FTW geometry in the HOB list comes from coreboot's active
SMMSTORE region. MM uses private coreboot callbacks bounded to that region. Ordinary public
SMMSTORE writes, erases and full-flash arming are denied while the service owns
the store; raw reads remain available. The protected capsule owner alone can
request bounded programming after authenticating signed metadata and image
blocks. Its callbacks preserve the live variable store and applicable immutable
regions; there is no separately armed public full-flash path.

Enabling the service enforces SMM-only BIOS writes even when an older stored
`bios_lock` setting is false. The runtime BIOS Lock option is suppressed in this
composition; intentional unlocked developer operation requires disabling the
service. Finalization checks effective EISS, BIOS Lock and interface-lock bits
and closed writes before returning to the payload. Failure stops boot.

The IA32 service reserves 4 MiB inside TSEG, in addition to the normal handler,
per-CPU stacks, stage cache, IED and optional OPAL state. The loader validates
the actual reservation and handler fit before exposing the service.

On S3 resume coreboot validates the retained layout and dispatcher pointer,
resets the external communication buffer and loads the normal coreboot SMM
handler again. It does not run the Standalone MM initialization entry twice.

## Payload handoff

The packed coreboot table record is 24 bytes: tag and record size at offsets 0
and 4, a 16-bit revision at offset 8, an 8-bit APMC command and flags at
offsets 10 and 11, a 64-bit communication-buffer address at offset 12 and a
32-bit buffer size at offset 20. It deliberately contains no service image,
SMRAM destination or entrypoint.

The first page of the shared buffer contains the request state and MM completion
status. The standard page-aligned MM communication message begins at offset
`0x1000`. The payload writes one bounded request, issues the advertised APMC
command and reads the response from the same allocation. coreboot rejects an
unsupported revision, invalid state or message larger than the fixed capacity.

