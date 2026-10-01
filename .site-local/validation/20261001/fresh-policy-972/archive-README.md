# Exact PR453 compatibility QEMU evidence

Source is `972e68eeabe1e4edcb452cb956f4151a76d4782a`, paired with coreboot
`4dd7974f5a5ec4a6e849cb76f0f81d1212645148`. The selected native image was
freshly built. Native image, native LVGL raster, pairing, three setup QEMU boots,
Linux runtime assertion, separate BGRT QEMU boot and table/bitmap assertion
all completed with exit zero. Root visually inspected readable Selecting text
and the navigated LVGL form.

ROM SHA256 is
`0da4952d317572ed3fa61cb515ea89e6c791d05fc656aa2ca865a7012f06bbac`;
native payload SHA256 is
`357741d6a4d8b2ce49807f45f927a57d9a1a14bdc90386a780af039c2a56f658`.
The pairing manifest retains source, profile, generated configuration/header,
composition and tools identities. Run manifests retain their original paths.

Initial setup invocation refused before guest startup because an ignored raw
fixture was absent. Retry1 refused before guest startup because a nested native
reference build lacked the explicit selected defconfig and changed prepared
inputs. The selected profile was restored and retry2 propagated its explicit
defconfig unchanged; its complete three-boot command returned zero. The initial
runtime assertion used the parent directory rather than the actual hotkey/run
directory and refused; retry1 against the actual manifest returned zero.
Failures are retained, not edited into passing receipts.

Only regular evidence files are staged. Per-run duplicate NVMe/USB disks and
TPM runtime state are excluded; single source fixture inputs are retained.
Pflash before/after images preserve the firmware-setup request transition.

This is Q35 debug legacy-variable-provider compatibility, not the protected
service or canonical-policy activation, release-profile or hardware validation.
PR453's actual protected producer/consumer join and full native suite receipts
are separately preserved on the validation evidence branch.
