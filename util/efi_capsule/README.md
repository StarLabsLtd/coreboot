# coreboot capsule tools

These GPL-2.0-only utilities create the authenticated, single-image UEFI FMP
capsule emitted as `build/coreboot.cap`. They are payload-neutral and therefore
do not fetch or build EDK2.

For the legacy EDK2 updater, `append_rmap.py` appends the selected FMAP region
allow-list to a copy of the ROM. The typed CDK2/coreboot broker instead receives
the exact final ROM: coreboot owns its bounded write-region policy, and its
authentication gate requires the payload size to equal the FMAP image size.
`DRIVERS_EFI_CAPSULE_REGIONS` is therefore a legacy-only setting; it is not an
authorization input to the typed broker. A production coreboot-owned capsule
composition must enable the hidden `DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT`
capability after installing its complete protected policy and endpoint.
`generate_capsule.py` adds the FMP payload/version header, signs it with OpenSSL
using the configured certificate chain, verifies that chain, and wraps the
result in the standard authentication, FMP image, and capsule headers.
`validate_capsule.py` checks the completed capsule's structure, flags, image
GUID, version, lowest-supported version, exact ROM bytes, RMAP-to-FMAP region
binding, signature chain, and embedded-driver count. It can additionally check
a supplied DXE firmware volume when diagnosing a payload that uses a resident
FMP driver.

Relative certificate paths for the legacy transport are resolved below the
actual repository checkout name derived from `EDK2_REPOSITORY`. Typed profiles
use coreboot-relative or absolute certificate paths.

Run the focused tests from the coreboot root with:

```
python3 -m unittest discover -s util/efi_capsule/tests
```
