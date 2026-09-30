# CDK2 payload

CDK2 is integrated as a source-built external payload. Select `PAYLOAD_CDK2`
and configure the exposed `CDK2_*` options with the rest of the coreboot board
policy. The coreboot build passes its resolved configuration, Kconfig tool,
target tools, and an output directory to the pinned CDK2 source tree.

The integration invokes CDK2's `coreboot-stage` contract, which resolves the
strict direct configuration and emits the complete image. The resulting
`build/cdk2/native/cdk2-coreboot-image.elf` is packed through coreboot's normal
ELF-payload path. No firmware volume, FFS file, PE seed, or prebuilt payload
module is accepted by this integration.

For provenance, the ROM contains both `cdk2/coreboot-config`, the resolved
outer coreboot configuration, and `cdk2/config`, CDK2's resolved nested
configuration.

The `config.emulation_qemu_x86_q35_cdk2_compat_runtime` fixture and
`test-cdk2-q35-compat-runtime` target provide bounded QEMU runtime acceptance.
They deliberately use Q35's legacy SMMSTORE provider and fixed VT-d test
topology, so they are compatibility/runtime evidence only. They do not replace
the separate source-built strict-direct, no-legacy production artifact gate.
Build and run that exact fixture with:

```
make KBUILD_DEFCONFIG=configs/config.emulation_qemu_x86_q35_cdk2_compat_runtime defconfig
make -j$(nproc)
make test-cdk2-q35-compat-runtime
```
