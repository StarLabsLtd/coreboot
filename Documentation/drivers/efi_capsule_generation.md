# Generating signed UEFI capsules with EDK2

coreboot can cooperate with an EDK2 payload to support firmware updates via the UEFI
ESRT/FMP capsule mechanism.

This document covers generating a *signed* capsule during the coreboot build.

## Build-time capsule generation

Enable the build-time capsule target:

- `CONFIG_DRIVERS_EFI_GENERATE_CAPSULE`: generate `build/coreboot.cap` after building the ROM.
- `CONFIG_DRIVERS_EFI_CAPSULE_REGIONS`: whitespace-separated FMAP region allowlist embedded into
  the ROM as a manifest (e.g. `COREBOOT EC`).

## Capsule signing certificates

`GenerateCapsule` can sign the FMP payload (PKCS#7). Many platforms require signed capsules.

coreboot exposes three Kconfig options for the certificate chain:

- `CONFIG_DRIVERS_EFI_CAPSULE_SIGNER_PRIVATE_CERT`: PEM containing the signing private key and
  leaf certificate
- `CONFIG_DRIVERS_EFI_CAPSULE_OTHER_PUBLIC_CERT`: PEM intermediate certificate
- `CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT`: PEM trusted root certificate

When building an EDK2 payload that verifies capsules (FMP), the payload must embed the same
trusted root as a PCD include file. coreboot expects that include file to exist next to
`CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT` using EDK2's naming convention, e.g.
`TestRoot.pub.pem` -> `TestRoot.cer.gFmpDevicePkgTokenSpaceGuid.PcdFmpDevicePkcs7CertBufferXdr.inc`.

If a configured path is relative, it is interpreted relative to the configured EDK2 repository
inside `payloads/external/edk2/workspace`.

The defaults use the EDK2 BaseTools test certificate chain. Do not use the test keys for
production firmware updates.

To generate your own certificate chain and convert it into the required PEM files, see:
`BaseTools/Source/Python/Pkcs7Sign/Readme.md` in the EDK2 tree.
