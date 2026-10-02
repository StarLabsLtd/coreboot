# Controlled native service component receipts

Consumer source: signed/SOB `bb3d590e82741aeca8043e8f14ec1987d9ecfb9a`,
parent `b337caff352a776f39f84df4a9d2188c34f31160`, branch
`agent/protected-native-qemu-flash-consumer` in StarLabsLtd/cdk2.
The exact eight reviewed source paths were frozen during execution. Signing
changed no source body; independent signed-source inspection and native replay
confirmed this. Production core, variable entry, RAM trigger and entry32 source
were not modified. The existing entry32 arrays are globalized only in the
component test object. HTTPS publication completed with actual exit 0.

Corrected independent producer source: signed
`323c989735d0c8998b119f6e3874d79490772016`, with service mechanism
`f9c0be48317b9d56532eceb46677ec8adee7390c`. The first passing author ROM was
built from the reviewed frozen mechanism before signing. The independently
fresh full ROM was built from the signed producer source.

## Scope

These are real controlled Q35 native boots: one CPU, `-cpu max`, 1 GiB RAM,
actual SMM installation and lock, 2 MiB TSEG, 16 KiB SMM stack, 64 KiB
SMMSTORE, and writable pflash with `cfi.pflash01.secure=on`. Source-built
uncompressed CBFS leaves are used, not host trigger/IRQ hooks or a substituted
backend seed. The real producer authenticates canonical loader receipts and
the actual reserved frame before publishing its endpoint. The consumer imports
the actual table, ACPI and mailbox allocation HOB. Its native-only linker
adapter executes the immutable flash OUTB leaf; it does not execute or establish
authority for the unchanged production RAM trigger.

The actual variable entry installs the policy into actual DxeCore/SecurityStub.
Actual CoreLoadImage requests a fresh real op9 snapshot and calls the real native
PE loader for SetupMode unsigned allow. Real IF/TPL observations require IF=0
and TPL=31 inside that operation, and restored IF=1/TPL=4 afterward. Ordinary
initial PK enrollment uses the documented SetupMode trust bypass; that initial
PK signature is **not** claimed to have been cryptographically verified.
Subsequent well-formed wrong-signer KEK is refused with KEK absent, PK-signed
KEK and KEK-signed db are cryptographically checked, and enrolled unsigned PE
is denied without invoking the native PE loader.

A genuine cold restart uses the first run's modified media and reads exact
PK/KEK/db ESL values and attributes, then obtains a fresh enrolled op9 denial.
An initialization-only native boot establishes an unenrolled PK-absent baseline
through the real SMM default-store recovery. A separately compiled actual Core
mutant discards only its CLI instruction; it must reach the intentional native
failure before enrollment and leave that initialized SMMSTORE unchanged.

This is not arbitrary-OS runtime authority, production boot-guard admission,
general platform admission, physical hardware validation or project completion.
The full production guard remains held. Parser host checks and native compiler
closure are separate from the native execution receipts below.

## Actual receipts

Times in the managed rows are systemd service wall time and CPU time from the
actual reaped tool result. The peer timed replay uses `/usr/bin/time` instead.

| Session | Actual result | Wall / CPU | Receipt |
| --- | --- | --- | --- |
| 96307 | 0, initial native compile only | 9.855 s / 9.809 s | `cdk2-native-service-component-first-compile.log` |
| 97422 | 0, persistence compile only | 10.015 s / 9.991 s | `cdk2-native-service-component-persistence-compile.log` |
| 92136 | 2, initialization-only unused image GUID | 10.473 s / 10.354 s | `cdk2-native-service-component-genuine-baseline-compile.log` |
| 54326 | 0, corrected four-ELF compile only | 10.306 s / 10.301 s | `cdk2-native-service-component-genuine-baseline-final-compile.log` |
| 89948 | 2, actual 1 MiB SMM stack/stub overlap | 71.207 s / 11.515 s | `cdk2-native-service-component-first-real-producer.log`, `run.sc2oJJ` |
| 46450 | 2, independent fresh 1 MiB ROM, same overlap | 70.448 s / 10.905 s | `cdk2-native-service-component-first-fresh-full-producer.log`, `run.jYw5kB` |
| 2990 | 2, actual 2 MiB SMM installed, uninitialized CBFS type | 70.577 s / 10.893 s | `cdk2-native-service-component-real-tseg2m-producer.log`, `run.JuGVCA` |
| 15400 | 0, genuine four native boots | 16.786 s / 16.978 s | `cdk2-native-service-component-real-rawtype-producer.log`, `run.Clu3Yt` |
| 53047 | 0, independent signed-consumer four-boot replay | not measured | `q35-native-service-peer-replay.log`, `run.lSBbC4` |
| 53744 | 0, independent fresh ordinary full producer build | not measured | `q35-service-peer-corrected-full.sTBtHD/build.log` |
| 57109 | 0, independent fresh-ROM four-boot replay | 16.50 s elapsed; 13.76 s user, 2.88 s system | `q35-service-peer-corrected-full.sTBtHD/native-replay.log`, `run.GBWeSu` |

The passing suites contain 12/8/6/7 exact serial stage markers for
positive/persistence/initialization/CLI-mutant runs. Normal, persistence and
initialization VMs return debug-exit status 3; the deliberate mutant returns 5.
Timeouts, faults and missing markers cannot pass. Independently compared media
joins are exact: positive after equals persistence before, initialization after
equals mutant before, and mutant before equals mutant after. All prior failed
attempts remain failures, not gate passes.

## Exact inputs and retained outputs

Author passing donor ROM SHA256:
`d7bc504d1d92c0ad7f1b1b13b8befd4962ed40843ce9bd37507194e2d9d2fa09`.
Independent fresh passing donor ROM SHA256:
`605b3c0751b2ab8833a7ef3613e209d93335f272cccda2ca7a3d329b6a08139b`.
Matching cbfstool SHA256:
`e2ea132a5bd001e002c7991b28e87f4f16c1c1c6d62af57721dbd5948436b186`.
Resolved full configuration SHA256:
`3350f3e76cc010804252333bddb6be6c9e4dd17a3b82be43250460406e7f64a5`.

Session 15400 ELF SHA256:

| ELF | SHA256 |
| --- | --- |
| `probe.elf` | `808d2f3b1a4af7433261de181aec19fdde37d06702e7189e8cdf5d52b49eb180` |
| `persistence.elf` | `114b3e472d6daf84ee1b7b5bb936526e44ae50986593d9c54fa6ad8f91fc1573` |
| `initialize.elf` | `464e574dac0b22555a2281db59a954979236ee4bc861098aef23fc23f48ae5fe` |
| `cli-discard.elf` | `4bcbe82b76257916a55478dd59f1c33c489d17c5c101d44b58f1db5702ee948b` |

The archive retains guest ROMs/ELFs/maps/serial logs/QEMU logs, actual source
mutants, public Auth2 inputs, per-run configuration and media snapshots, all
listed raw logs, the fresh full producer ROM/tool/config and its peer README.
Generated private fixture keys, intermediate objects and NVMe scratch images
are deliberately excluded. No live hardware or HA state was touched.

`guest-receipts-with-fresh-full.tar.zst` SHA256:
`0f88dc3f62793dbd168cf61117a411e30d6fde8e8f30a60c5359f1dba2232a45`.
`source-snapshots.tar.zst` SHA256:
`25c236e6485e8f6635c632cf5706140090305a730bd6f870f295341e5c6959c4`.
The latter copies the exact signed eight consumer paths and nine producer
mechanism paths after testing, with no intervening source changes. It is not
represented as a source snapshot captured before execution. The older initial
archive is also preserved separately.

The exact named native gate is:

```sh
make native-protected-variable-service-component-test \
  COREBOOT_ROM=/home/sean/.cdk2-q35-native-loader.o3NKUu/build/coreboot.rom \
  CBFSTOOL=/home/sean/.cdk2-q35-native-loader.o3NKUu/build/util/cbfstool/cbfstool
```

The peer replay substitutes its independently built fresh ROM and matching
tool, sets `CDK2_CONFIG_READY=1`, and uses the same named gate. Every guest ROM
is an isolated copy; the source donor is not mutated. Initial attempted
publication to an incorrect repository URL failed with exit 128/not found;
the corrected actual-origin HTTPS push reaped 0, without a source body change.
