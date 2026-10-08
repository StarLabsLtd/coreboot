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

The protected Q35 SPI/BMP native-menu profile pins the reviewed native CDK2
source at `f8fa78ed14969fa1e50d718515afc634e78c48b5`. Its public EFI variable-store
reader supplies RAM/disk handoff metadata; it is not the legacy writable
SMMSTORE backend. The artifact gate requires generated outer SMMSTORE/updater
and nested native SMMSTORE FVB/FTW switches to be zero. Protected variable
Runtime also requires the source-attested public MM service route.

The paired artifact-policy HOST regression uses explicit retained build inputs:

```
make test-cdk2-external-payload-artifact-policy \
  CDK2_POLICY_PRODUCER_OUTPUT=/absolute/resolved-producer-output \
  CDK2_POLICY_NATIVE_OUTPUT=/absolute/retained-native-output
```

The producer output contains `full.config` and `build/config.h`; the native
output contains `resolved.config`, `include/cdk2/config.h`, and the genuinely
built `native/cdk2-coreboot-image.elf` and `native/cdk2-elfcheck`. This HOST test
models CBFS IO while executing the actual retained native ELF checker. It tests
the reader/backend policy and exact input bookends, not a complete toolchain
seal, packed-ROM equivalence, a menu-route build, or guest acceptance.

Actual menu qualification additionally requires the existing producer wrapper's
new nested build, selected source/tool/config proofs, extraction of the packed
payload and comparison of its loaded entry/addresses/bytes to that nested ELF,
and byte-identical packed outer/nested config entries. Historical PAYLOAD_ELF
descriptors and guests are not rebound to this route. The profile's absolute
logo and trust-certificate paths are qualified local inputs, not portable CI
defaults. No native-menu ROM or guest qualification is claimed by this policy
regression.

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
