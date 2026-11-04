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

The BIOS Lock option remains available with this service. When enabled,
finalization checks effective EISS, BIOS Lock and interface-lock bits and closed
writes before returning to the payload. Failure stops boot. When disabled,
direct OS flash access is intentionally available; the service still authorizes
its own requests but cannot protect against writes that bypass it.

The IA32 service reserves 4 MiB inside TSEG, in addition to the normal handler,
per-CPU stacks, stage cache, IED and optional OPAL state. Supported configurable Intel client
platforms reserve 16 MiB when enabled. GLK/GLKR retain their FSP's 8 MiB TSEG
and remain outside the build-qualified board opt-ins.

Capsule verification and programming retain fixed 64 KiB blocks, independently
of the configured SMMSTORE logical block size. Existing board defaults remain
unchanged: Lite GLK/GLKR use 4 KiB; the other Star Labs boards use 64 KiB.
The service continues to use the configured variable-store geometry; it does
not detect, migrate or change the layout of an existing store.

The loader checks the actual reservation and handler fit. Configuration and
linked-size checks do not measure peak heap or stack use. AMD and QEMU are not
supported by the Intel-specific flash and SMM backend.

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

## OPAL S3 ownership

Only coreboot-enumerated NVMe devices with assigned memory resources are eligible
for S3 enrollment. The native owner checks that trusted snapshot before accepting
a secret. With the fixed MM service enabled, enrollment also requires the retained
trusted-boot window to remain open. The first external image, ReadyToBoot or
ExitBootServices closes that window; clearing credentials does not reopen it.
Unlocked builds without the fixed service retain their previous lifecycle policy
and are not covered by this authorization guarantee.

The payload connects trusted consoles and native NVMe drivers before external
images. Its coreboot transport forwards only pending credentials; other LockBox
backends retain full snapshots. Late-discovered devices may boot but are not
enrolled for S3 unlock. Resume checks the enrolled device identity, restores the
authorized BAR with decode/DMA disabled, and validates the resulting base after
D0 before any controller MMIO. VID/DID/class matching is resource ownership
validation, not unique physical-device authentication. These checks do not
establish timeout-time DMA isolation or prove enrolled-drive recovery on hardware.
