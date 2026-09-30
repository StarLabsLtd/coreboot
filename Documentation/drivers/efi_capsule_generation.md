# Generating signed UEFI capsules

coreboot can generate `build/coreboot.cap` from the final ROM without EDK2
BaseTools. The same generator serves two deliberately distinct update
transports:

- The legacy EDK2 transport appends an RMAP region allow-list to a copy of the
  ROM. EDK2 consumes that manifest and owns the update operation.
- The typed coreboot transport signs the exact final ROM. Its protected broker
  derives writable routes from coreboot's runtime policy; build-time RMAP data
  is not an authority input.

`make capsule` builds the capsule explicitly. A normal build also creates it
after the ROM is finalised when `DRIVERS_EFI_GENERATE_CAPSULE` is enabled.

## Transport selection

The capsule format is selected by a resolved transport capability, not by a
payload name:

- `DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT` is derived from an EDK2 payload with
  `DRIVERS_EFI_UPDATE_CAPSULES` enabled.
- `DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT` is hidden. A production platform
  composition may default it on only after installing the complete protected
  broker policy, FMP owner, authentication provider and published endpoint.

`DRIVERS_EFI_GENERATE_CAPSULE` is unavailable without one of these transports.
The two update paths are mutually exclusive.

## Firmware identity and version

The generated FMP capsule uses:

- `DRIVERS_EFI_MAIN_FW_GUID` for its ESRT/FMP image identity.
- `DRIVERS_EFI_MAIN_FW_VERSION` for the attempted version.
- `DRIVERS_EFI_MAIN_FW_LSV` for the lowest supported version.

When the configured version is zero, the build parses the leading
`<major>.<minor>` value in `LOCALVERSION` and encodes it as
`(major << 16) | minor`. A zero LSV inherits the resolved firmware version.

The generator emits one authenticated FMP v3 image. It signs the MSS1 header
and exact image bytes followed by the little-endian monotonic count, matching
the payload-mm verifier.

## Legacy RMAP and embedded FmpDxe

`DRIVERS_EFI_CAPSULE_REGIONS` applies only to the legacy transport. It lists
the FMAP regions included in the RMAP manifest, for example `COREBOOT EC`.

Some legacy platforms load `FmpDxe.efi` from the capsule. Enable both:

- `DRIVERS_EFI_CAPSULE_ACCEPT_EMBEDDED_DRIVERS`
- `DRIVERS_EFI_CAPSULE_EMBED_FMP_DXE`

The EDK2 payload build must already have produced `FmpDxe.efi` and `DXEFV.Fv`.
The coreboot capsule recipe passes the former to its generator, then checks the
embedded-driver count and verifies that the matching FFS driver identity exists
in the latter. The explicit artifact variables are
`CAPSULE_LEGACY_FMP_DXE` and `CAPSULE_LEGACY_DXE_FV`; their defaults point at
the selected EDK2 build architecture and build type.

Typed coreboot capsules reject embedded drivers. Their endpoint and
authentication provider are already resident in protected firmware.

## Signing policy

Configure:

- `DRIVERS_EFI_CAPSULE_SIGNER_PRIVATE_CERT`
- `DRIVERS_EFI_CAPSULE_OTHER_PUBLIC_CERT`
- `DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT`

Legacy relative paths are resolved below the checkout name derived from
`EDK2_REPOSITORY`. Typed profiles use coreboot-relative or absolute paths.

The host tools reject inputs outside the payload-mm production verifier's
bounds: certificates must use 2048- to 8192-bit RSA keys, a CMS object may
contain at most eight certificates, each certificate is limited to 64 KiB,
certificate bytes are limited to 256 KiB, and the complete CMS object is
limited to 256 KiB. The signed body is limited to 128 MiB minus the monotonic
count. The generated signature uses SHA-256 and detached DER PKCS#7.

The default EDK2 test certificates are compatibility fixtures only. Production
firmware must configure its own protected signing hierarchy.

## Validation

The build validates the completed capsule before publishing it atomically. It
checks the FMP/authentication layout, identity, versions, exact image bytes,
signature chain and transport-specific rules. Typed images additionally must
contain exactly one runtime-compatible FMAP with bounded, uniquely named,
non-empty `FMAP` and `COREBOOT` regions. Legacy embedded-driver builds also
validate the matching DXE firmware volume.

Run all generator, hostile configuration, Make recipe and production verifier
tests with:

```
make test-efi-capsule-tools
```
