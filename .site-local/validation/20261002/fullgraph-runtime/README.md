# Protected fullgraph runtime diagnostic receipts

## Durable packet and combined linear replay

The accompanying `fullgraph-runtime-098f-receipts.tar.zst` has SHA-256
`f4f964deeca56a4dd5593fe4c050709aba1cdb4a1c8ed26b536acadca35a19e5`.
Its qualified author/peer receipts and changed-source archives are described below.

Root independently replayed the combined linear stack at signed
`d5ec82592fd4069315e30c337e10497ed8ac01c6`, including the capsule policy,
predicate consumer, diagnostic and duplicate BDS capsule retirement. Actual
session 50764 reaped 0, elapsed 1m32.691s, CPU 1m40.951s, peak 416.5M.
`root-combined-native-d5ec.log` retains the three real boots: unchanged guard,
genuinely missing endpoint and successful converted runtime ordinary NV access.
This does not prove enrolled image authorization, Auth2 runtime writes or hardware.

The separate first BDS host aggregate exited 2: root supplied a nonexistent
inventory target, and a stale order-test mutation assumed only one DISK token
after publication legitimately added a second. Its raw failure is retained in
`root-bds-host-d5ec-failed.log`; it is not a successful aggregate. The reviewed
test-only correction targets the actual disk decision rather than publication.
The corrected BDS/disk/order/native-direct-image-inventory aggregate actually
reaped 0 in session 11020 (43.597s service elapsed, 1m18.104s CPU, peak 334M);
its log is `root-bds-host-corrected.log`. The only source change after d5ec
is the independently reviewed order-test correction, signed `957d1f2d7d`.
The capsule-retirement author's broader `native-dxe-core-test` also actually
reaped 0 in session 86602 at signed `9b729e83b0359cbd2bcebe31216496cd7f066622`
(6m58.209s elapsed, 13m26.636s CPU, peak 346.4M), retained separately as
`bds-author-full-native-core.log`; its ten-path production diff exactly matches
root d5ec's signed linear copy.

The two root linear predicate/runtime regression logs are actual exit-0 runs
at signed `a5db484eabb11486cc3bd48b1240a3c2e8e03c31`, taking 43.065s and 4.118s
respectively. They are separate host regressions, not additional native boots.

This is a controlled, single-CPU Q35 secure-pflash diagnostic. The successful
graph changes exactly one generated outer protected-config return statement;
the signed production guard remains unchanged. It is not production activation,
hardware validation, arbitrary-OS/platform support, or enrolled fullgraph Secure
Boot/image-authorization proof. The runtime writes here are ordinary NV writes,
not post-conversion authenticated writes.

## Signed source identities

- Diagnostic checkpoint: `a013f765cecdd974d8ace173ebd9b14d0236e521`.
- Final configuration-isolation fix: `098f5373ff6769012527a914b2733020d1b03e9f`.
- Protected consumer mechanism: `e483613a68e41c9879dc696afdf837c84b72b7f5`,
  copied without tree changes to `02506714b4cd379552bad4e616fd698585863fb2`.
- Producer mechanism: `636f67a94129a4606b05c35076054003a01cf9a5`.

The source archives contain the eight diagnostic paths at final 098f, ten
consumer changed paths at e483, and fourteen producer changed paths at 636f.
They are changed-file snapshots, not complete source/build closures. The full
scratch source-input archives and NVMe media remain in the original run
directories and are not included here. No fixture private keys are archived.

Source archive SHA-256:

- `signed-diagnostic-source.tar`:
  `c73dd556613eedbc89a1aadb64adda60823fd78cdae1f43726672ffdbcf255d0`
- `signed-consumer-source.tar`:
  `6b5a75b50a597bdd25e9aa4e4fb83a6a08f58fba861ca6e51a501532e9ab2259`
- `signed-producer-source.tar`:
  `36f412c02888b01e89d6b88716649146aff010bfe1ab43c8930da2956291f472`

## Final actual executions

Author session **53531 actually reaped 0** via the managed unit
`cdk2-public-fullgraph-predicate-native-isolated`, invocation
`4e46de2576314addb46f40eab357dd2f`. Service elapsed 1m29.367s,
CPU 1m37.915s, reported memory peak 397.8M. Raw log:
`/tmp/cdk2-public-fullgraph-predicate-native-isolated.log`.
Original artifacts:
`build/cdk2/native/protected-variable-fullgraph-diagnostic/run.EEODfX`.

Independent session **26530 actually reaped 0**, directly running the same final
script against the peer's independently rebuilt producer ROM. No trustworthy
total elapsed/CPU measurement was recorded. Raw log:
`/home/sean/fullgraph-predicate-peer-isolated.log`.
Original artifacts: `/home/sean/fullgraph-predicate-peer-isolated/run.KZLS0z`.

Both executions used the final one-file isolation fix before signing; signatures
changed metadata, not source bodies. Both now retain their actual scratch
`resolved-payload.config`, unchanged across normal and diagnostic image builds.
The actual header/config require protected variables, strict direct linear boot,
native Variable/Security2 services, the genuine attested service-route input and
DMA-handoff consumption. Legacy FVB/FTW remain absent.

Author command was the named Make gate, with TMPDIR=/home/sean and the actual
positive full.config passed as COREBOOT_CONFIG:

```
make COREBOOT_CONFIG=/home/sean/q35-predicate-fwinfo-full.xUjMtj/full.config \
  COREBOOT_ROM=/home/sean/q35-predicate-fwinfo-full.xUjMtj/build/coreboot.rom \
  CBFSTOOL=/home/sean/q35-predicate-fwinfo-full.xUjMtj/build/util/cbfstool/cbfstool \
  COREBOOT_ABSENT_ROM=/home/sean/q35-linear-no-endpoint-fwinfo.19jrz0/build/coreboot.rom \
  CBFSTOOL_ABSENT=/home/sean/q35-linear-no-endpoint-fwinfo.19jrz0/build/util/cbfstool/cbfstool \
  native-protected-variable-fullgraph-diagnostic-test
```

Positive donor build **21491 actually reaped 0**; source bodies were frozen
through native execution and match signed 636f. Peer fresh build **49831 actually
reaped 0** and produced the same ROM/tool bytes. Author and peer build logs and
full configs are retained in `producer/`.

- Positive original ROM SHA-256:
  `2f819511d5eb4e5b3fe962d6052078cb5af55ac7bc89bb9283aeb30952b089ef`
- Matching positive cbfstool SHA-256:
  `771789538ee11962d821d8c2e3fd64886b64a1dd54b0ee33a88cc459c59583c1`
- Author full.config SHA-256:
  `3ceb57f06890f60a4f7d77f5f6c3fecc8d9ad8fae8561cf9953bb0f0ba0c6920`
- Genuine no-endpoint original ROM SHA-256:
  `66a85df72dc5e8c4b91a1c649fef1d6bec888a860ee143ce8d43ad8b412b0fe9`
- Matching no-endpoint cbfstool SHA-256:
  `e2ea132a5bd001e002c7991b28e87f4f16c1c1c6d62af57721dbd5948436b186`

The positive donor genuinely selects protected state-predicate ownership,
resource/DMA handoffs, Q35 VT-d, measured LAPIC timing, linear framebuffer,
SMBIOS and FW_INFO. FW_INFO uses the established Q35 test identity
00112233-4455-6677-8899-aabbccddeeff, version/LSV 0x001a0009 and actual 8MiB ROM.
The no-endpoint donor is a real service-disabled build, not record stripping or
an invented endpoint: its service/public/attested selections are absent, while
the joined resource/DMA/timer/framebuffer/FW_INFO profile is retained. Its natural
TSEG default is 1MiB rather than the positive CMS/service profile's real 2MiB.

## What the three actual boots prove

1. The untouched packaged graph returns exact EFI_UNSUPPORTED. Read-only QMP
   checks the actual ELF-specific CLI/HLT backedge, RAX, IF0, CPL0, SMM0 and HLT.
   No diagnostic app executes; protected media is unchanged.
2. The same diagnostic ELF on the genuinely absent endpoint producer reaches
   actual VariableRuntimeDxe/VARIABLES_MIN NOT_FOUND, no variable installation,
   policy-protocol count 0 and no app. The equally strict exact-status QMP oracle
   passes and protected media is unchanged.
3. The diagnostic positive VM exits **3**, with all seven stages exactly once:
   APP_ENTERED, READY_CLOSED_OP7_IF1_SET, REAL_EBS_CALLBACKS_LIVE,
   REAL_SET_VIRTUAL_MAP, RUNTIME_PHYSICAL_ALIASES_REMOVED,
   VIRTUAL_GET_SET_CLOSED_OP7 and RUNTIME_PASS. These are actual packaged driver
   PEs, CoreLoad/StartImage, ReadyToBoot events, EBS callbacks and runtime-image
   relocation. Subsequent Get/Set and closed op7/op9 checks use converted real
   runtime slots after removal of every actual EFI_MEMORY_RUNTIME physical alias.
   The alias offset is 128GiB. Ordinary setup-mode NV writes change protected
   SMMSTORE media. Caller IF1/IF0 restoration is observed, not interrupt delivery.

The NVMe disk is cloned from the existing validated GPT/ESP fixture, canonical
SHA-256 `054a451e67b291adc7390301ef21c9df2de3582e768612cc66459e58d1305e36`.
The actual compiled app replaces BOOTX64.EFI at the audited 1MiB ESP offset.
All three per-VM app readbacks compare byte-identically to the compiled PE.
The actual producer's DMA table is validated and consumed; NVMe uses genuine
mapped DMA. The producer's EDU default-deny observation is distinct from this
runtime proof. No VT-d or authority check is disabled.

Final author and independent peer image/config hashes are identical:

- Normal ELF: `987037166c9b322fe7b4478d038b590b0958fa557ce046c3bafc361e73f0063d`
- Diagnostic ELF: `222cf1818a157849a2d1b9f8938ef08a3d0aba9a1ad3119f193382268658e082`
- App PE: `26d221c41afccac68e3723248f3a3dc09e44777d9c6409b3b2d7ebc5648091e3`
- Actual config header: `0c546ae56b41930751e06f37a322e2f17ab06985ac07a5a98a135d652b4e46e4`
- Resolved payload config: `3fc9107add0ef66ada641925a8ce26437b7b691d2d0cbae4d5093251342789ab`

## Preserved failures and earlier qualified receipts

All thirteen surviving author raw logs are copied verbatim to `raw-history/`.
They are not collectively counted as passes.

- Initial app compile failures: wrong source-first prerequisite produced GCH
  objects; subsequent PE-GC link failed. The final real ELF-GC closure/PE link
  was compile-only success, not native execution.
- The first generator repeat test exposed existing-output overwrite due to a
  set-e AND-list mistake. Explicit file/symlink rejection fixed it; the peer's
  repeat/hardlink/dangling-symlink tests passed. Temporary repro logs are retained.
- First pair: 683ms, aggregate 2, inherited READY/config shortcut.
- Second pair: 478ms, aggregate 2, real protected legacy-FVB Kconfig conflict.
- 99811: aggregate 2, required legacy driver inventory defect, positive VM124.
- 65384: aggregate 2, missing real donor FW_INFO, positive VM124.
- 28127: aggregate 2, real protected type3 ownership gap at ESRT, positive VM124.
- 64986: aggregate 2 despite positive VM0: explicit terminal-failure capture,
  not runtime success. The normal and genuine-absence oracles passed.
- First new-MM invocation: aggregate 2 in 89ms; missing outer COREBOOT_CONFIG
  correctly refused before the runner.
- 96516: aggregate 2, 2m50.121s; genuine new MM/ESRT progressed, but diagnostic
  consumer DMA was disabled against active producer VT-d; NVMe timed out, VM124.
- 76435: aggregate 2, 1m45.066s; genuine NVMe mapped DMA worked, but an invalid
  fixed-media FAT superfloppy was not a proper ESP. Terminal capture VM0 remains
  NOT_PASS, never a success oracle.
- 74745: aggregate 0, 1m28.543s. All three native boots really passed, but receipt
  audit found the outer recursive Make's individually exported CONFIG/KCONFIG
  paths redirected .config writes to the owned worktree build directory. The
  compiled config and images were correct and no concurrent writer existed;
  this earlier pass is qualified as not fully configuration-isolated. Final
  53531/26530 repeat the proof with explicit scratch paths and saved configuration.

Failed old runs and the successful original runs remain recoverable in their
original ignored artifact directories. This packet contains the final author
and independent peer ROMs, images, readbacks, before/after SMMSTORE, config,
inventory, generated/production entry source and logs, but not the full NVMe
disk images or private fixture-key material. Signing/source snapshots occurred
after testing; the peer separately checks their exact body parity.
