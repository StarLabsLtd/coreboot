# Native public authvar RAM transport checkpoint

The signed consumer checkpoint is `05da8a58a1e73ca25d72715ea5cc8c0bd56646e3`
on `agent/authvar-public-native-runtime-probe`, parent `7a86e9a811` (the
signed `-x` restack of root's real lifecycle closure fix `4ba3da583920`).
The four changed paths were independently source-reviewed before compilation
and independently receipt-reviewed before signing. Signing did not change the
tested source body. Snapshots archived here are after-testing snapshots, not
claimed pre-test snapshots.

## Actual receipts

* Compile session `89599`: actually reaped 0, managed unit
  `cdk2-public-ram-first-compile`, wall 12.270 s, CPU 12.238 s, peak 39.9 MiB.
  Target: `make CDK2_CONFIG_READY=1 native-protected-variable-public-service-compile-test`.
  Raw log: `/tmp/cdk2-public-ram-first-compile.log`.
  SHA256: `696aec7ede8f91907fe8ac7b739fcd85ba265318740da29b3721bb804aa04fd7`.
  This is compilation only, not native execution proof.
* Native session `57605`: actually reaped 0, managed unit
  `cdk2-public-ram-first-native`, wall 20.562 s, CPU 20.660 s, peak 104 MiB.
  Invocation ID: `ac49030f47c440daa2d0134bbb3d75bc`.
  Raw log: `/tmp/cdk2-public-ram-first-native.log`.
  SHA256: `ac00f9405d27a3ca02f2d62d6c2763f5e05dcbd8e68df00e099b401338d7d95e`.
  Fresh artifacts: `run.DwYc8x` beside this directory.

The native command was:

```sh
make CDK2_CONFIG_READY=1 native-protected-variable-public-service-component-test \
  COREBOOT_ROM=/home/sean/q35-public-service-build.w9EUt4/build/coreboot.rom \
  CBFSTOOL=/home/sean/q35-public-service-build.w9EUt4/build/util/cbfstool/cbfstool
```

Both managed runs used this worktree, `TMPDIR=/home/sean`, a 65536 KiB stack
limit and the host compiler/tool path; output went directly to the raw files,
without a pipe that could obscure the actual process exit.

## Input identities

* Source-built producer ROM:
  `d80a815d7fba0a7404e9aeb7217d2f49356801666d6a73f67e4ad2e1e92fc357`.
* Matching cbfstool:
  `e2ea132a5bd001e002c7991b28e87f4f16c1c1c6d62af57721dbd5948436b186`.
* Actual embedded producer config:
  `592794477f62db2732a5fd083cdafbb9aea0c950a5b326d6b80df5e8749467b2`.
* The compiled-input manifest itself:
  `aee0044d3a209aeebcc59fb55b8ffbe3f6b17d8f7623e6b89cbe31e41349511d`.

The retained native receipt archive is `native-receipts.tar.zst`, SHA256
`cbb15372c2342d682e09825212c6bffc72c66ee30f52af30d8a150bc21315bd6`.
It includes the actual four copied/modified pflash images, native ELFs, serial
and QEMU logs, SMMSTORE before/after snapshots, exact compiled Core mutant,
resolved config and input manifests. It excludes private keys and NVMe images.
The nine-file after-testing signed-source snapshot is `signed-source.tar`,
SHA256 `ddda300cfa25a0457c86b13f3b789ed8f7867f60aba827abaf41355b42fbb4cc`;
it records the four checkpoint paths and their directly relevant unchanged
production/test sources, not an asserted complete compiler closure archive.

The donor genuinely selected
`Q35_SMM_INVOCATION_NATIVE_PUBLIC_SERVICE_COMPONENT`, with one CPU, 2 MiB
TSEG, a 16 KiB SMM stack, actual canonical bootstrap/service installation and
secure pflash. The harness copied the donor and modified only each copy's
payload, never the donor ROM.

## What the four native boots establish

The public profile links and executes the unchanged production RAM
`cdk2_authvar_native_x86_trigger`; the flash trigger wrapper/helper is absent.
The existing observer wrappers delegate to the real image loader and snapshot.
Normal public Get calls retain IF0 and IF1; valid Auth2 Set calls retain IF1.
Wrong full saved RAX, unrelated latched APMC `0xfb`, and a COMPLETE op6 replay
leave the actual mailbox unchanged. Subsequent genuine calls recover, and real
CoreLoadImage op9 still operates, proving those attempts did not enter runtime.

The suite retains ordinary SetupMode PK enrollment (initial PK trust bypass,
not cryptographic initial PK verification), wrong-signer KEK refusal,
PK-authenticated KEK, KEK-authenticated db, SetupMode unsigned-image allowance,
and enrolled unsigned-image denial. A second cold boot reads the enrolled
PK/KEK/db bytes from the first boot's actual pflash without reenrollment.
The initialized-unenrolled boot supplies the actual media baseline for the
one-line, compiled CoreLoadImage CLI-discard negative; its SMMSTORE remains
byte-identical. The independent reviewer read 15/10/8/9 serial stage markers
for normal/persistence/initialization/mutant and verified the exact mutant.

This is a controlled single-CPU Q35 secure-pflash public RPC component proof.
It is not general interrupt delivery, unrelated chipset/multi-source SMI,
multi-CPU, arbitrary OS, hardware, full production boot, EBS, virtual-runtime
conversion, or closed op7 proof. The precise private cold-bootstrap origin
contract remains separate, and the full production protected boot guard is
still held. No production source or admission flag changed in this checkpoint.
