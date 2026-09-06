# Coreboot capsule tools

These GPL-2.0-only utilities create the authenticated, single-image UEFI FMP
capsule emitted as `build/coreboot.cap`. They are payload-neutral and therefore
do not fetch or build EDK2.

`append_rmap.py` appends the selected FMAP region allow-list to a copy of the
ROM. `generate_capsule.py` adds the FMP payload/version header, signs it with
OpenSSL using the configured certificate chain, verifies that chain, and wraps
the result in the standard authentication, FMP image, and capsule headers.
`validate_capsule.py` checks the completed capsule's structure, image GUID, and
embedded-driver count. It can additionally check a supplied DXE firmware volume
when diagnosing a payload that uses a resident FMP driver.

Run the focused tests from the coreboot root with:

```
python3 -m unittest discover -s util/efi_capsule/tests
```
