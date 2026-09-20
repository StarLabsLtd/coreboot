# Capsule update contract

The revision-2 `LB_TAG_CAPSULE_HANDOFF` ABI describes the facts a payload needs
before it may expose a firmware-management protocol.  The record binds the
existing firmware GUID and version to exact image and boot-media bounds, erase
geometry, SMMSTORE exclusion and explicit source-to-flash regions.

Production publication must fail closed unless coreboot establishes all of
these facts itself:

* the image's canonical CBFS `build_info` was authenticated;
* exactly one live `LB_MAINBOARD` identity exists and matches `build_info`;
* FMAP, erase geometry and boot-media bounds are authoritative.

The board identity is deliberately not a new GUID.  A routing GUID supplied by
an outer capsule is not authenticated identity.

The backend contract has one operation: apply the validated region plan while
preserving every unlisted byte and verifying readback.  It provides no raw
read, write or erase operation and is separate from SMMSTORE variable traffic.

## Implemented prerequisite

The contract library now has a private bounded-writer prerequisite. It accepts
only a plan exactly equal to the backend's immutable route policy, validates
the complete plan before its first media operation, erases and writes only
listed erase-aligned regions, and compares readback after every block.

The public contract still exposes only one `apply_regions(plan)` operation.
Raw media callbacks are private implementation details, and the writer is not
connected to SMMSTORE. Host tests prove preservation, exact readback and zero
media operations for hostile preflight inputs. The contract fixture emitted by
the coreboot test binary is passed through the current CDK2 SystemFmp parser
rather than only compared as bytes.

`Q35_CAPSULE_UPDATE_TEST_PROOF` is default off. It compiles this prerequisite
for the QEMU Q35 target but does not publish a record or install a broker.

## 26.09 source audit and production gate

The 26.09 tree provides some authoritative facts, but not the full chain:

| Fact | 26.09 source | Status |
| --- | --- | --- |
| Current media extent | `boot_device_ro()` / `boot_device_rw()` | Available |
| FMAP regions | `fmap_locate_area()` and bounded subregions | Available |
| QEMU erase geometry | QEMU pflash `QEMU_FLASH_BLOCK_SIZE` | QEMU-only |
| SMMSTORE exclusion | `SMMSTORE` FMAP region | Available when configured |
| Live board record | `lb_mainboard()` | One record is emitted |
| Canonical image identity | CBFS `build_info` | Present, not authenticated on ordinary Q35 |
| Board-to-image binding | `LB_MAINBOARD` versus `build_info` | No authenticated comparison hook |
| Payload-callable broker | Dormant typed one-shot SMM contract | Unselected; no producer or dispatcher |
| Fixed communication/staging ownership | Reserved bootmem allocation | Q35 test-only; no DMA proof or publication |
| Protected writer scratch | Fixed SMM-module arena | Q35 test-only; uninstalled |
| Durable update checkpoint | None in this contract | Missing |

The dormant broker ABI and state machine are described in
`Documentation/lib/capsule-broker-contract.md`. Production publication remains
gated on an authenticated current-ROM
`build_info`, a unique live-mainboard match made inside that trust boundary, an
authoritative platform erase-geometry provider, and a distinct runtime broker
transport with a durable checkpoint. The existing full-flash SMMSTORE mode is
not that broker and must not be widened or reused.

## Deliberately unavailable

`CAPSULE_UPDATE_CONTRACT` defaults off. No producer exists: in particular,
there is no caller-supplied boolean or metadata path that can assert trust.
No table producer, broker caller, SMI dispatcher or production flash backend is
registered. Publishing the record before all production gates
above exist would turn metadata into authority.

The standalone test emits a 144-byte fixture for byte-exact comparison with
the CDK2 consumer and exercises ordinary, optimized and sanitized hostile
inputs:

```
tests/lib/capsule_update_standalone_test.sh
```
