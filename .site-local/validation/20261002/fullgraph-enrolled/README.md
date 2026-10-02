# Enrolled packaged-graph diagnostic: 247bcab2d0

This is a **test-only one-outer-return-discard diagnostic**, not production
admission. The production protected boot guard, Core image authorizer, native
RAM trigger, runtime variable owner and coreboot producer bodies are unchanged.
The actual complete native inventory and real relocated runtime PEs execute;
no trusted/started flags, fake protocols, host producer pipe or authorization
callback substitutes are used.

## Source and artifact identities

- Signed/SOB diagnostic source: `247bcab2d01172e560dd63aa71e00ff30cb6c9c1`.
- Parent: `ff080c6e41bd48206c04196c43047741693689e8`, the reviewed linear
  diagnostic/config-isolation copies atop
  `a5db484eabb11486cc3bd48b1240a3c2e8e03c31`.
- Signed producer: `636f67a94129a4606b05c35076054003a01cf9a5`.
- BearSSL: `8ef7680081c61b486622f2d983c0d3d21e83caad`.

The author built before metadata-only signing. All six changed source bodies
were compared byte-for-byte against the actually tested scratch source before
signing. The source archives are signed changed-file snapshots made after
testing, not complete source/build closures. Complete source-input tarballs
and private fixture build directories remain in the original run directories;
private signing keys and full NVMe disk images are deliberately not archived.

Author runtime application PE SHA-256:
`f7b5f742cd6167076a4464423decb902ca0d1a69658436a3391947f0494a4643`.
Author normal ELF SHA-256:
`ce5b7a7fa7370b7b73989c3650b340464a3ac1635faaa1f16b2f1c5bf6586c3a`.
Author diagnostic ELF SHA-256:
`ef44fd59c19886338b4971e79d460dcd372a47bf3061c64df85972251026fdeb`.

Each execution generates fresh public test certificates/signatures and embeds
its own genuine inputs, so independently built enrolled application hashes need
not match. The donor producer ROM/tool identities must match.

## Author actual execution

Session **48960 actually reaped 0**, managed unit
`cdk2-fullgraph-enrolled-first`, invocation
`686e3c9cbe7f43e28a1da08757e2548a`.
Measured wall `1m41.864s`, CPU `1m50.836s`, peak memory `385.7M`.
Raw log: `/tmp/cdk2-fullgraph-enrolled-first.log`.
Original artifacts:
`/home/sean/Documents/.cdk2-worktrees/protected-fullgraph-enrolled-auth2/build/enrolled-gate/native/protected-variable-fullgraph-diagnostic/run.ZySW2z`.
The exact command and timestamps are retained in `author-unit.log`.

The named gate was
`native-protected-variable-fullgraph-enrolled-diagnostic-test`, with explicit
isolated outer build/defconfig and genuine producer `COREBOOT_CONFIG`. The
scratch runner explicitly owns its own config/header/Kconfig paths and checks
post-build config equality. Source-before/after checksums and reverse whole-TU
comparison establish exactly one generated outer guard return discard.

## Independent actual execution

Session **61429 actually reaped 0**, direct script execution against the peer's
own fresh producer build 49831, whose ROM/tool bytes match the author donor.
No total elapsed/CPU timing was measured; none is inferred from the author run.
Raw log: `/home/sean/fullgraph-enrolled-peer.log`.
Original artifacts: `/home/sean/fullgraph-enrolled-peer/run.EFaCXj`.
The exact unchanged signed six source paths were independently compared to the
peer's actually executed source snapshot, all byte-identical. All 18 enrolled
positive markers, exact normal/absent register/refusal oracles, three app
readbacks, config/source identity checks, VM3 and media comparisons passed.
The peer released the source reader and accepted this bounded diagnostic only.

## Genuine donor composition

Positive producer ROM SHA-256:
`2f819511d5eb4e5b3fe962d6052078cb5af55ac7bc89bb9283aeb30952b089ef`.
Matching cbfstool SHA-256:
`771789538ee11962d821d8c2e3fd64886b64a1dd54b0ee33a88cc459c59583c1`.
Producer full.config SHA-256:
`3ceb57f06890f60a4f7d77f5f6c3fecc8d9ad8fae8561cf9953bb0f0ba0c6920`.
These are the genuine joined Q35 public service/state-predicate owner, resource
and DMA handoffs, VT-d backend, measured LAPIC timer, linear framebuffer,
SMBIOS and FW_INFO profile, with 2 MiB TSEG/16 KiB SMM stacks and no legacy raw
SMMSTORE writer. Actual secure pflash rejects non-SMM writes.

Genuine no-endpoint ROM SHA-256:
`66a85df72dc5e8c4b91a1c649fef1d6bec888a860ee143ce8d43ad8b412b0fe9`.
Matching cbfstool SHA-256:
`e2ea132a5bd001e002c7991b28e87f4f16c1c1c6d62af57721dbd5948436b186`.
This separately resolved/built donor disables service/public publication
naturally, retaining the real joined resource/DMA/timer/framebuffer/FW_INFO
composition. It is not a stripped or fabricated endpoint table. Its natural
service-off TSEG is 1 MiB; that difference is not hidden.

## What the three boots establish

1. Untouched protected packaged graph returns exact `EFI_UNSUPPORTED` into its
   actual ELF-specific CLI/HLT transfer loop, with IF=0/CPL0/SMM0/HLT register
   proof, no application, and unchanged protected media.
2. The SAME diagnostic ELF on the genuine no-endpoint donor reaches real
   VariableRuntime owner/entry `NOT_FOUND`, VARIABLES_MIN failure and no
   installed variable-policy protocol or application. Exact NOT_FOUND/transfer
   loop register proof and unchanged media are required.
3. The positive diagnostic executes the real full graph and already-started
   OS-loader application after genuine ReadyToBoot. It performs real PK, KEK
   and db Auth2 updates with exact ESL/attribute readback. **Initial PK setup
   enrollment uses the actual documented SetupMode trust bypass**, not a claim
   that initial PK CMS was trusted. KEK is PK-signed and db is KEK-signed.
   Actual SetupMode=0/SecureBoot=1 is read, not inferred from PK/config.

   Actual Boot Services LoadImage refuses an unsigned child and an independently
   rogue-signed child, then loads/starts a db-signed proper PE. Its separate
   compiled child emits an independent execution marker and returns through
   real CoreStart; Core performs its ordinary automatic application unload.
   These calls are **after ReadyToBoot, before ExitBootServices**.

   Real closed policy registration, ordinary NV Get/Set and IF=1 caller
   restoration remain required. Genuine Core ExitBootServices invokes real
   runtime callbacks. Real RuntimeDxe/VariableRuntime PEs relocate via
   nonidentity SetVirtualAddressMap, after which every actual runtime physical
   alias is removed. All runtime slots are converted; only converted real
   runtime Get/Set slots are then used for variable calls.

   A strictly newer KEK-signed db Auth2 with a distinct ESL owner is accepted
   and read back. Exact same-byte replay is rejected and leaves the new value
   unchanged. A separately valid later CMS signed by the untrusted rogue is
   rejected and leaves the value unchanged. Caller IF=1 restoration is checked
   on all three calls; enrolled mode, closed op7 and runtime-only op9 refusal
   remain required. The positive VM must exit **3**, emit all **18** unique
   markers and actually modify protected media. A terminal phase failure or
   VM0 cannot satisfy the positive oracle.

## Scope and preserved predecessors

This establishes controlled, single-CPU Q35 secure-pflash public RAM transport,
actual enrolled boot-image allow/deny and authenticated virtual-runtime updates
within a one-return-discard diagnostic. It does not activate the production
guard or prove arbitrary OS/platform/interrupt-source/hardware support, every
firmware UI/capsule behavior, or cold enrolled MAIN-app boot selection. The
parent app is genuinely admitted/started while initially in SetupMode; only
its child admission is tested after enrollment.

No failed enrolled VM preceded this first author successful run. The redundant
test-only child Unload call was removed by source review before compilation,
because real CoreStart already unloads a returned EFI application. Previous
ordinary-variable app compile, generator overwrite-refusal, Kconfig,
VARIABLES_MIN, ESRT, DMA-profile and disk-format failures remain durably
preserved/qualified in the accepted predecessor packet
`fullgraph-runtime-098f-receipts.tar.zst`, SHA-256
`f4f964deeca56a4dd5593fe4c050709aba1cdb4a1c8ed26b536acadca35a19e5`.
Its ordinary NV proof must not be relabeled as enrolled/Auth2 proof.
