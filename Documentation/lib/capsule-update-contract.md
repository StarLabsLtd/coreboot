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

## Deliberately unavailable

`CAPSULE_UPDATE_CONTRACT` defaults off and only compiles a structural validator.
No builder, table producer, broker caller or flash backend is registered.  This
coreboot base does not
provide a single trustworthy path that supplies authenticated `build_info`, a
unique live-mainboard comparison, authoritative writable FMAP routes, erase
geometry, boot-media bounds and a verified bounded write operation.  Publishing
the record before all of those hooks exist would turn metadata into authority.

The standalone test emits a 144-byte fixture for byte-exact comparison with
the CDK2 consumer and exercises ordinary, optimized and sanitized hostile
inputs:

```
tests/lib/capsule_update_standalone_test.sh
```
