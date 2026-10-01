# Selected MTL SMM artifact receipts, 2026-10-01

These generated archives preserve the exact native and pinned-crossgcc artifacts
used by the optional stack diagnostics. They are kept on a separate evidence
branch, not proposed as production source or release firmware. No hardware was
flashed and no final ROM, FSP execution, legacy-route or OS runtime-variable
cutover validation is implied.

Selected production source: `7113e9f08e7fc08a682bcd28570711285a7aff99`.
The later test-only producer fixture checkpoint
`ab10062ba55422d0c31e216ff4a22dd9bffb15c1` changes no selected SMM sources.
Native diagnostic source: `979a6a33500d895ff9189f4e9a5ecf9ada68af75` (PR 333).
Release fixture/diagnostic source:
`fd5afd4cc71dda2246089de508f89b0e44369ca5` (PR 334). Its three-file delta
changes test/diagnostic inputs only, not selected production sources.

| Generated archive | SHA-256 |
| --- | --- |
| `mtl-selected-native.tar.gz` | `1ca0bec67233859ca4fe2d209e88bb248633043c82e82423a8d932556dfaac9e` |
| `mtl-selected-release.tar.gz` | `3effffa10672b830ce45b9ba2e582a3b0647c508da611a86073d5be344782581` |

Each archive contains OFF and ON `full.config`, `smm/smm.elf`, `xcompile`,
`smm-build.log` and `config.log`, plus all 394 ON compiler `.ci`/`.su` files.
It excludes FSP binaries, other build objects, credentials and toolchain
executables. The source and license texts for the compiled code remain in
the corresponding repository revisions and submodule pins.

Extract an archive into a newly created scratch directory. Run the diagnostic
from the corresponding source revision against the extracted `ON/smm` path:

```sh
node tests/mainboard/starbook_mtl_authvar_selected_stack_audit.js ON/smm
node tests/mainboard/starbook_mtl_authvar_selected_stack_audit.js \
    --release-recorded ON/smm
```

The first command is for the native archive; the second is for the release
archive. The tool checks the ELF, configuration and exact annotation bytes
before traversal. Relocating the extracted files does not change the relative
annotation-path digest. Updating a recorded hash is not acceptance of changed
source, compiler, callback bindings or configuration.

Both recorded artifacts yield nested C maxima of 9,908 BOOT and 12,004 runtime
bytes, with no unresolved reachable edges. Adding the reviewed entry frames,
call slots, stub alignment and canary allowance gives conservative totals of
10,531 and 12,627 bytes within the selected 16,384-byte per-CPU stack. These
remain artifact-qualified calculations, not live hardware admission.

The release compiler is the actual coreboot-pinned
`i386-elf-gcc (coreboot toolchain v2026-07-28_508d4deeddc) 15.2.0`, executable
SHA-256 `2bb7e369e87dcb22736938d7e9cae0b7d408199114eb0a31c7ceb5f873cd3c6d`.
Both release profiles disable `ANY_TOOLCHAIN` and check the compiler selections
in the generated `xcompile` after their actual links. Native mode deliberately
uses the separately recorded Ubuntu GCC 15.2.0 artifact.

## CDK2 compatibility-profile native check

`cdk2-427-native-check.tar.gz` has SHA-256
`e88f4c90c88c1df066f3bce4bdc34dc82ebc5aa0280d5b2cc20c14d1b4ad8354`.
It preserves the completed root whole-check and packaging logs, exact `.config`
and native `cdk2-coreboot-image.elf` for CDK2 PR 427, source
`421c236df2b6fbb67a85a9efefd800c7fc7c155a`.

The actual commands returned success:

```sh
TMPDIR=/home/sean make -k -j4 CDK2_CONFIG_READY=1 \
    CDK2_BUILD_DIR=build/cdk2-426-acceptance check
TMPDIR=/home/sean make CDK2_CONFIG_READY=1 \
    CDK2_BUILD_DIR=build/cdk2-426-acceptance native-coreboot-image
```

The recorded runs used the absolute equivalent of this build directory.
It started fresh at PR 426, whose whole check stopped on the transport test's
obsolete blanket source-text gate. It then continued at the test-only PR 427
boundary correction. This is a completed frozen-source run, not a second
independent clean build. Configuration SHA-256 is
`6942c16419bb25e1ce8e760bf383bc7ffbcbc9fcf80f77c55e6a968f5baffe24`;
native image SHA-256 is
`9195876b13cd9cb5a3e5e781fd68563a9cf46f32593cbfb58456b82d12768545`.
The selected provider is legacy SMMSTORE compatibility, not protected-owner
activation. These are native checks and an ELF, not a composed coreboot ROM,
fresh QEMU boot, hardware validation, or a receipt for later source revisions.

The separately fresh PR 428 run is preserved in
`cdk2-428-native-check.tar.gz`. It contains the completed whole-check and
packaging logs, `.config` and native image for frozen CDK2 source
`dd05a60b87502cce58ac0ffe1edba1b14930c435`. Both commands above returned
success using `build/cdk2-428-acceptance`, which copied only the configuration,
not objects, from the preceding build. Configuration and image hashes are
unchanged. This is one fresh native build, not two independent builds or a
QEMU/hardware/protected-activation receipt.
Archive SHA-256 is
`7ce04f02e783ea740ff32dc827447a79393f2db10b931fd398797832c8881be0`.

The completed frozen PR 431 run at
`06511e02c46e8cae5cbbfd3cbf1894dc22441fd4` is preserved in
`cdk2-431-native-check.tar.gz`, SHA-256
`4060881c498fecd87c1cf1612651110473955db64664739093bc3ace53d8fc8b`.
The same configuration and existing `build/cdk2-428-acceptance` directory were
used; this is not another clean build. Whole native `check` and
`native-coreboot-image` both returned success after the dormant importer in
PR 430. The ELF SHA-256 is
`1387af6699b64a79e3184030e8a70d48f41d596cb11370f88115a50f6b746297`.
The archive also preserves three passing command-cache regression logs,
the root native32 focused importer log for PR 430, and root/independent
authority integration logs for test-only coreboot PR 338
(`db5f66f12b31a7b1f983c01d9b7a510eeb133b82`). These do not replace the
separately qualified older release SMM artifact receipts above, and establish
no new full-ROM, QEMU, protected-owner activation or hardware result.

## Separate post-classifier release SMM receipt

`mtl-selected-release-after-339.tar.gz`, SHA-256
`907b18a763a340fa9bd80b0e7a316cefa35b630490de7429dd63db05fe8ac077`,
preserves the fresh OFF/ON SMM links at coreboot
`cdc87a8610cf6fffa0547fcc7188c9961c458ab9`. It includes each profile's
configuration, ELF, compiler selection and build logs, plus all 394 ON
`.ci`/`.su` files. The selected classifier changes are included; the new
BOOT-private lease is disabled and has no symbols in this artifact.

ON ELF SHA-256 is
`5f839e8b6bfdf477651c4e91c04e314979ed57bc58eddb35a9339eb371e6d64a`;
configuration remains `6016a34ab0c6957a05a3a5935876b8c17556e291e33ec67f450d1530ca385aa2`;
sorted annotations are `f430443957b5852d4a32abcc42682bb9a1e59a54b368d3bcba061b40239a8d8c`.
The separate `--release-post-classifier-recorded` diagnostic mode in PR 341
replays this artifact without accepting it through either historical mode.
Root source/assembly and linked-edge review found no new classification
callback or recursion; artifact-qualified nested maxima remain 9,908/12,004
bytes. This is not a whole-ROM or selected-lease/lifetime/hardware receipt.

## Fourteen obsolete dirty review-state recovery snapshots

`coreboot-retired-review-fourteen.tar.gz`, SHA-256
`8ada804c8f9f8d7bda453ea57fe0178eec3fdeaad172e5c0d5746229e08c71c4`,
preserves exact staged/unstaged changes, source checkpoints, incremental bundles
and relevant untracked/ignored artifacts for fourteen old review worktrees.
See `coreboot-retired-review-fourteen.md` and the enclosed manifest for the
41 repository states, narrow public-reference exceptions and replay commands.
This is recovery evidence only; it neither approves those patches nor records
build/boot validation or worktree retirement.

## Exact Q35 compatibility runtime and setup receipts

The two `qemu-reviewed-433-runtime-setup.tar.gz.part-00`/`part-01` files
reassemble the generated archive in that order. They are split below GitHub's
per-file limit, not separate test runs. SHA-256 digests:

| Artifact | SHA-256 |
| --- | --- |
| Part 00 | `be3d64bc6b49a4681ccfaafca2e42f60352b9306cb2089468b4e955478566e8f` |
| Part 01 | `fbf1a7848877611fe21e4bdd2c0cd487c336d5f21ece80be93ee386da5d2bba0` |
| Reassembled archive | `b6835888c1a1cccd884a278ee3b9d609883b8abf1d0927415a0a0448d4ba0eb2` |

Coreboot source is signed PR340 `e276a6cb6ea2d910942c0f0bddefb49f83c823c4`,
with CDK2 PR433 `dacf554220bc629d121b4046a0fa51d19ca62136`. The normal ROM
digest is `788bf0b8a43155049250d8104a5bead937b2d94768bb4d3224563244f3d573e1`.
Its fresh shutdown-enabled QEMU run and runtime-services linear oracle pass
Linux boot, EFI variable create/update, CBMEM/SPI gates and S5. The earlier
no-shutdown run reached Linux/S5 but returned 124; its strict oracle refusal is
retained, not rewritten as success. Initial empty compatibility-store warnings
are present; these receipts do not claim warning-free logs.

The separate setup profile has 36 direct images, ROM digest
`65ac1b2f5d15e7726cd2a0c258f4093e83eb00edcb7909f5492214f2793f36d2`,
and payload configuration digest
`b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb`.
Genuine QEMU F2 entry, Down navigation, Escape/BGRT restoration and Linux
continuation pass. The persisted OS firmware-setup request and following boot
present setup without F2, consume the request and continue to Linux. Root
visually inspected the readable LVGL controls, changed help and STARLABS logo.
Debug `SplashStatus` is `UNSUPPORTED` for this CDK2-rendered logo; visible status
text remains an open fix, not a successful visual check.

The archive preserves manifests, raw serial/debug logs, SPI/pflash bytes,
screenshots, source-owned synthetic test disks and generated requester fixture,
the composed ROMs, setup configuration and direct-image provenance. It excludes
TPM state and sockets. Runtime/hotkey replay passes independently after fresh
extraction; the original-location complete two-boot oracle also passes.
The unchanged archived manifests retain absolute source paths, so the complete
two-boot oracle refuses a relocated pflash path chain. No path check was removed
and no manifest was edited. Independently replaying the real variable-store
transition on the three archived pflash images passes; requester fixture bytes
and manifest match their originals. This is not a fully location-independent
two-boot oracle replay.

All runs use the bounded Q35 DMA-test topology and legacy SMMSTORE compatibility
provider, not the dormant protected owner. These are not a whole release matrix,
production Secure Boot activation, new MTL lease proof or hardware validation.
The separately retained PR434 setup log records a fresh three-boot run of that
later consumer on the same coreboot PR340 base; its independently composed ROM
is byte-identical to the PR433 setup ROM. The archive itself contains PR433 runs.

## PR435 continuing native suite and paired-component receipts

`cdk2-435-continuing-check-and-paired-receipts.tar.gz`, SHA-256
`ff6c9b3f8ef9d73317a7804fdd44bdfe8bfb98ee266d7033c30f4716cbf01492`,
preserves the root full native suite and packaging passes at CDK2 PR435
`165157d5f3e8c7978bf6b60d669f7ebbb6e09b0d`. The build directory started
fresh at PR434; that run failed in the nested review-profile diagnostic.
PR435 fixes that test's inherited Make environment. Its continuing suite and
package checks pass using the same directory, not a second clean build.
The original failure and the focused root/independent-peer diagnostic passes
are retained alongside the successful logs.

The selected `.config` digest is
`6942c16419bb25e1ce8e760bf383bc7ffbcbc9fcf80f77c55e6a968f5baffe24`;
generated `config.h` is
`995d60054d1f3c0f59c567e7e4bbd278079bc3954eeec483ed6c6ee21bf747b5`;
the retained native coreboot-image ELF is
`7b1cf403502c63cdd1c264c0a7b95cc4329073d486711f13b7274725ed065cf9`.
All three actual files are included. These receipts do not record a new ROM,
QEMU run or protected-owner activation.

Additional root logs record PR436's private policy-copy primitive (including
native32 and sanitizer cases), actual selected table import and real
producer/client/Security2 join. The source reply is erased before the copied
policy is consumed. Selected table-import sanitizer coverage is separately
qualified in the PR, not claimed by this normal root importer run.
PR343's actual loader and coupled all-CPU receiver gates are also retained.
Their loader placement and held-lease callback modeling remain explicit;
these are not hardware, resumed-CPU copy-coherence or public-runtime proofs.

## Separate private-delivery selected release SMM receipts

`mtl-private-delivery-release-selected.tar.gz`, SHA-256
`8c6a0d60ddcc8df85ed87d2bcfeaf2188016a53effec38d6866b1d3909b29cb7`,
preserves two actual OFF/ON SMM-only links using the pinned release crossgcc,
production source PR343 `df3b5db2890377bac91210c430839472588ff51d`, and
the separately reviewed opt-in test profile. The normal profile default stays
private-OFF. The selected ON configuration has private receipt delivery enabled;
prepare/close/consume are retained, while held begin/recheck remain discarded.

The recorded ON ELF is
`4b339979a62be5b5d248b6ea92d42df43792ec826aa7f4f0b0a390256ff97893`,
configuration is
`a3123ad1f869777c1c8f7c7e57f0e2b1d5a1564c11f7c5d8ead5336631f61ee4`,
and sorted 396 compiler annotation paths/contents are
`5c6e51feadfe605d3199fa7aa2b2be1a2100c12860ac385e6ab867652130ce7f`.
Root and independent peer exact-artifact audit replays pass: nested root
maxima 9,908/12,004 bytes, receipt receiver 3,196, and lease prepare 1,724.
Outer entry allowances remain separately qualified by actual assembly.

The independently built ON ELF is
`a1a18937d7b1865d3bebfc8892960c554ef61d2892249e69d1dadd8b521121a1`.
Its configuration is identical. Actual section comparison finds only nine
temporary-directory bytes different in `.debug_line_str`; every other ELF
section matches. One `static.ci` graph title also has that build path, yielding
annotation digest
`d7cb9dea2646be79354c0f3f97fc037681de1a6d457d0824c2067804720af3b1`.
The unchanged exact-record audit correctly refuses this distinct artifact;
that refusal is retained rather than normalizing its identity or claiming
another accepted recorded replay.

Both configurations, compiler selections, build logs, ELFs and ON annotations
are retained, with peer/root recorded-audit logs and path-difference evidence.
This is SMM-only evidence with fixture FSP headers, not a complete MTL ROM,
hardware admission, tag-0x58 publisher, resumed-CPU protection, policy-copy
coherence, terminal loader lifetime or public-runtime activation proof.

## PR436 continuing whole-suite receipt

`cdk2-436-continuing-native-suite.tar.gz`, SHA-256
`1a9c863dfb439a07ade79efcc12ffba9e37b750704b543499c55ea868f6abf63`,
preserves the root complete native suite and confirmed package passes at
`6532bc8b50345f7a4b3b41450536f2687ee49e09`. This continues the existing
PR434-origin build directory, not a clean independent build. Configuration,
generated header and native image digests remain the exact PR435 values
listed above; those three files are already retained in the PR435 archive.

The first execution-session run ended with status 143 before completion;
its partial log is not a pass. An isolated managed-user-service retry failed
because its initial tool path lacked `kconfig-conf`. The following retry with
the normal tool path passes with service `Result=success`, `ExecMainStatus=0`,
and no remaining process. Both failed/partial logs and explicit unit results
are retained alongside the successful suite and package logs. This adds no
ROM, QEMU, public-owner activation or hardware validation claim.

## PR437 actual compatibility splash and setup runs

Concatenate the two numbered parts of
`qemu-reviewed-437-runtime-status-setup.tar.gz` in order. The archive SHA-256 is
`eb0f7375fb940272828099392554eee2fdec0a4aca64a1b3189b4ad7d2219b2a`;
part 00 is `fe245d7d840c22fd994516298d259914c7c0382d29750e090cd442eb26d89407`,
and part 01 is `9dae4bc208811b4d4d964eb0a6085fba56317e76392e1462b7cf1a40a517eb6d`.
It preserves the actual coreboot PR340/CDK2 PR437 setup pair, ROM and direct
composition proof, screenshots, UART/debug/SPI output, fixtures and pflash
transitions. TPM sockets/state and process IDs are excluded. `SHA256SUMS`
covers the retained raw files and logs.

The exact paired ROM is
`497f9b8563298945f2990f775d966f510c62cfee9f96b64829a358df4434a3ae`.
The three real setup boots pass F2 entry/navigation/Escape-to-Linux, an OS
firmware-setup request, and its next-boot presentation without F2. The separate
boot-policy-marker capture reaches Linux, EFI-variable operations, TPM and S5.
Root visually inspected the actual captured splash: status text appears but
overlaps the wordmark, so this is evidence of the remaining presentation defect,
not final splash sign-off. Delegated coreboot-logo ownership is not selected.

The first native-image invocation lacked imported coreboot profile settings
and failed admission; its log is preserved separately from the successful
actual `coreboot-stage` invocation. This pair uses the compatibility variable
owner and bounded Q35 topology, not protected-store activation or hardware.
Relocated firmware-setup manifests retain original absolute pflash paths;
do not rewrite those paths to manufacture a successful original-chain replay.

## PR438 native suite and actual splash/setup captures

Concatenate numbered parts of
`qemu-reviewed-438-native-runtime-splash-setup.tar.gz` in order. Its SHA-256 is
`8974dd696f8e19115f746bc5e77eca58269a7571f0fd90d179cb041825c84582`.
Parts 00/01/02 respectively have SHA-256
`d2a044d58410e8800991c0fe7df228c1faf0d045acd3072e45cce72e25de1853`,
`343a23bc62ac079a8b47e72679fb58acf70716dcd2b8e93027348b57542441ea`,
and `d40b2dd67567bf4cab59084bab2093652ee175ecae083ebb354c28d23630298a`.
All retained files are covered by the enclosed checksum manifest; TPM state,
sockets and process IDs are excluded.

At signed CDK2 PR438 `f897871873403dc848b8250bf84339f3059c1d13`, the whole
native suite records managed-service success, exit zero and no remaining PID.
It continues the PR434-origin build directory, not another clean native build.
The actual `native-coreboot-image` packaging target also passes. The preceding
nonexistent `package` alias invocation is preserved as a preexecution refusal,
not a firmware failure or successful packaging result. Configuration/header
digests remain the PR435 values; the new native image is
`4b4ca6ba7c32870f22457361e701aaab2d0c3be9c48c19384772e5abd7ed02f3`.

The separate fresh setup-profile link was built from reviewed WIP `3efabc5b5d`,
whose entire Git tree exactly equals the signed PR438 tree
`cfcf12dd4fa24139e44b87c60403115c158f1a03`. Pairing uses the clean signed
head and unchanged coreboot PR340. Setup configuration is `b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb`;
its ELF is `57578467d994bc215b94461cfbd556a5949f42a968cbc9dbdb80101da2ada0e2`.
Both actual configuration/header/ELF sets are retained.

The paired ROM is `17069f762d27a3b853e66f1fbf9d0188f36b4ea3b62998c2068d902b250abbde`.
Actual QEMU runtime, F2/navigation/Escape and persisted firmware-setup runs
pass independently. Root and peer visually inspect a genuine Starting-OS
capture with the entire wordmark and readable text in its own below-logo strip.
The separate boot-policy capture has no text: source control flow prematurely
clears it before the hotkey wait. Both captures are retained; this is not final
waiting-splash sign-off. The compatibility variable owner and bounded Q35
topology remain explicit, with no protected-owner or hardware activation claim.

## PR440 actual waiting splash and setup receipt

Concatenate numbered parts of `qemu-reviewed-440-runtime-splash-setup.tar.gz`.
The archive SHA-256 is
`0ab48f9f0b41fac65cb2fdd12d86d7f0d79271782cd9a216f1cda1c71977df60`.
Parts 00/01/02 respectively are
`65e52f983bcce63f9e9763d1b7c355e276f5b91cb9b909eb69e96dde2d23f7a0`,
`9e8211df462143640b23ef861e60e72a963c4bb3e7464c2b5a27e06edd7c49aa`,
and `f5d5223def0cb0d94fb0edac79df328f8738ccea9bde151053136acae2723bba`.
The enclosed checksum manifest covers retained files; TPM state, sockets and
process IDs are excluded.

This is the fresh setup-profile build at signed CDK2 PR440
`fda69156c2576b9942190fa9951f3f9cf95135d5`, following the hotkey-wait fix in
PR439. The actual 36-record composition, strict MTRR and ELF checks pass.
Configuration remains `b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb`;
the native ELF is `4a8028b30d5c98c6956445fa766be2434429c5d6718178c20842bf04eb69a841`.
Pairing uses unchanged coreboot PR340; the ROM is
`17875170eb8c5a8f894110872944a5e9890438f1be02cb666bf33ca034997d14`.

Actual runtime and three setup boots pass: F2/navigation/Escape continuation,
an OS BootToFwUI request, and subsequent setup presentation without F2.
Root and independent peer visually inspect the complete wordmark and readable
Selecting-device text in its own below-logo strip in the captured hotkey-wait
frame. Independent oracle replays use the original paths. Relocated
firmware-setup manifests retain their absolute fixture/pflash identities;
do not rewrite them to manufacture an original-chain replay.

This is compatibility-variable-provider, bounded-topology QEMU evidence,
not a complete whole-suite receipt at PR440, delegated coreboot splash owner,
protected-store activation or hardware sign-off.

The subsequent PR440 continuing whole-suite attempt failed with exit 2:
its managed runner omitted `/usr/sbin` from PATH, so the loader-matrix fixture
could not locate the installed `mkfs.vfat`. The raw log and failed unit result
are retained, not counted as a whole-suite pass. Separately, the stable lint
gate rejects the standard `_Generic` keyword added by PR440's deadline test;
an exact-keyword lexer correction and regression tests are being reviewed.

## Genuine coreboot PR346/CDK2 PR440 splash composition

Concatenate numbered parts of `qemu-reviewed-346-440-coreboot-splash.tar.gz`.
Its SHA-256 is `9f3d313291ba097a36c903adcc07945c41079962d3838642e3ed69fdb1aa09b7`;
parts 00/01/02 respectively are
`01a17f7b78103936edd65b32add34b1d468320002b20e1e6216da9f4410523a0`,
`465f6f51a0aeddd95815dee529b29ca05b046c95e497953fbffa0b803b2e8c9e`,
and `9aa8448bf6ecd75f4eb72e73b27df2958914e0406d508fecb9a89a49b7bb4f29`.
The checksum manifest covers the retained files; TPM state, sockets and process
IDs are excluded. Original-path identity qualifications remain unchanged.

The actual coreboot build at signed `199d045ebe93f973c89eb854a6a406b608f6ce73`
selects BMP_LOGO and USE_COREBOOT_FOR_BMP_RENDERING with a real linear
framebuffer. Its config is `d6008d778842c4040b2e8c1a72f65cd59002c0c46fc906888473e02991b8469a`.
The unmodified configured StarLabs BMP is
`a05b76f687b176e39376a5fe2e1955cf6f5cd36435b9ef20ab66bb28d5f3909a`.
The managed build finishes with success, exit zero and no remaining process;
actual ramstage ELF, build log and bitmap are retained. Its committed baseline
CDK2 gitlink remains PR433, not PR440. The genuine compiled producer ROM is
subsequently paired through the normal strict tool with the separately fresh
PR440 36-record native setup payload documented above. The final paired ROM is
`c7c14c3d92632e054b06750a5d577facc00f156c690956f4e2d48f64cbaa467a`.

Actual UART records the retained BOOT SPLASH allocation, successful splash
adoption, status rendering and BGRT publication. Linux detects the BGRT table.
Actual runtime, F2/navigation/Escape continuation and the persisted BootToFwUI
request/presentation pass independently. Root and peer visually inspect the
configured blue StarLabs logo and readable Selecting-device caption below it.
The 234-by-234 logo crop at (283,183) matches the decoded configured BMP byte
for byte both before setup and after returning from setup; reference and actual
RGB files are retained. This is the genuine coreboot producer/native consumer,
not a host-synthetic DISPLAYED flag or another logo draw owner.

This validates the selected Q35 owner composition, not platform-wide early-DMA
activation, protected variable ownership, the complete final loader/capsule
matrix, or hardware. Linux table detection does not assert every BGRT field or
OS presentation behavior; those remain separate validation requirements.

## Exact CDK2 PR445/coreboot PR346 validation

Concatenate `qemu-reviewed-346-445.tar.gz.part-aa`, `part-ab` and `part-ac`
in that order. Whole archive SHA-256 is
`fd01dfcec3d0e2af8ef9f72ee71f287adf0b77ffbf00d8257bdcd4ab1ba2d867`.
The three part hashes respectively are
`a74c01651641bcb7bf80cd39b0d78a88c372228b8d8236734526a6040db8200d`,
`82a75e51a7ee6b63f8d6dceaea106183366838fd87a5a340c9c1e95cfb381b91`,
and `d855e6baefcc79e39a9ca80ee95a02754458ba2d10eb0d72d10a226b516cbf92`.
The enclosed checksum manifest covers retained files; TPM state, sockets and
process IDs are excluded. Absolute fixture/pflash identities are preserved.

Exact source is signed CDK2 `9291e98a5fe6e9b18462fe5f412541f8c32abf63`.
The continuing `make -j4 check` succeeds with the complete tool PATH, including
the newly aggregated real-PE PCI configuration-toggle gate. Its managed unit
is inactive, MainPID zero, Result success and ExecMainStatus zero. Separate
explicit stable lint and actual native-coreboot-image packaging also succeed.
This directory originated at PR434; it is not another fresh whole-suite build.
Continuing config SHA-256 is
`6942c16419bb25e1ce8e760bf383bc7ffbcbc9fcf80f77c55e6a968f5baffe24`,
and packaged ELF is
`d249c53541ac8c9c8b37c1938d27752feb4f6cefed4a4c3e52016b808b611ba1`.

The separately fresh 36-record setup composition passes native layout, strict
MTRR and coreboot gates. Its config SHA-256 is
`b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb`,
and native ELF is
`78311211f29acfb4f7d742acd02a54f3268b2106221b5d9ebc1c5cf75df5b8ac`.
It is paired with the genuine previously compiled coreboot PR346 producer
described above, not claimed as a new producer build or committed gitlink pin.
Final ROM SHA-256 is
`d5e9f46d5f0c32de884fe00228d511ecc8ff5a1bd19d2f27c97e25585abc806d`.

Actual QEMU runtime and three-boot setup acceptance pass: F2 navigation,
Escape/logo restoration/Linux continuation, OS firmware-setup request and
subsequent setup presentation. Root visually inspects real status and LVGL
controls; both before/restored RGB crops match the configured BMP exactly.
Linux detects BGRT. An independent peer replays the actual original-path
runtime/hotkey/firmware-setup oracles, verifies all 90 retained-file checksums
in a fresh extraction, confirms exclusions and compares both logo crops.
Relocated fixture identity refusals are not rewritten into false passes.
These compatibility-provider QEMU results are
not protected-owner activation, every BGRT field, hardware or final sign-off.

## Separate selected held-op9 SMM record

`mtl-selected-held-op9.tar.gz` has SHA-256
`2c46bf25219df9577178642f94807ac2697ea89ebeab2b4842ca3e6a853328fc`.
It preserves the fresh actual pinned-crossgcc OFF/ON SMM-only links from the
reviewed production checkpoint `c1faacd3da6011d75eb2430871d402144624272c`.
The source-equivalent final clean three-commit split is
`d159942f5e77425c84e81a75988ee3741ea23653`, directly after coreboot PR347.
No dirty submodule changes are included in those commits.

The new `--release-held-op9-recorded` diagnostic binds ON ELF
`62b897781711fe574d601668e29630be083a0c4d602afc9bb604a0272d55fe5c`,
configuration `0b5be5aa9b706d8f29e73b3815522022bf3378100d347578ebc5f51497ef8a33`
and 396 compiler annotation paths/contents
`4fadc1727ba8d8112f261f05f35813e724b1427f0c7d38c1d50d60e4c9f95b4b`.
Compiler identity remains the pinned executable documented above. Configs,
ELFs, xcompile selections, build logs, compiler annotations and component/audit
logs are retained; FSP binaries, toolchain executables and signer material are
not included. The checksum manifest covers every retained file.

Root and independent peer replay the actual graph and inspect the linked
BEGIN/transaction/RECHECK/copy/COMPLETE order. New strong delivery, held verifier
and live walker nested maxima are 2,252/2,160/1,280 bytes. The newly calculated
selected BOOT/runtime maxima remain 9,908/12,004, with no unresolved edges,
unbounded recursion or unknown native targets. Reviewed outer allowances give
10,531/12,627 within the selected 16,384-byte per-CPU stack. Returning delivery
checks are not simultaneously nested with the transaction's CMS path.
All four historical recorded artifacts still replay unchanged and reject the
new artifact through their historical modes. Root also replays the copied
annotation tree at its new path without changing any identity.

Independent component execution covers unchanged production and C fixtures;
the later runner-only narrowing of causal assertions is independently source/
generated-mutant/log reviewed, not claimed as another whole recompilation.
Actual missing-BEGIN and missing-RECHECK mutants fail at their specific media
and pre-copy assertions, rather than arbitrary sanitizer failure.
An independent fresh extraction verifies all 476 retained-file checksums,
exclusions and the exact recorded graph. This SMM-only link does not prove first native-entry origin, resumed
CPU copy lifetime, terminal loader/EBS/S3 closure, public variable activation,
hardware placement, a whole-ROM or final project sign-off.

## Committed coreboot PR349/CDK2 PR445 composition

Concatenate `qemu-reviewed-349-445.tar.gz.part-aa`, `part-ab` and `part-ac`.
Archive SHA-256 is
`3f136425dbf9facb34f73bafdddac7858920ad815ea3ab0eea00c25219627cde`.
Part hashes in that order are
`f419df288e5915b767619801c2fa0e8cef8f2c7fe029c8ccea7ce286e30f8225`,
`b28972025107e5793624d64304a2d304bcec595f748944f4731211e37a83e55b`,
and `9623605bde3f4a81b8be7edac9f2b18f5df36e2760c6d0e46dca968a317e1bba`.
Checksums cover every retained evidence file except the checksum manifest
itself; TPM state, sockets and PIDs are excluded.

The sole reviewed gitlink change at signed
`4dd7974f5a5ec4a6e849cb76f0f81d1212645148`, directly after PR348, selects
CDK2 PR445 `9291e98a5fe6e9b18462fe5f412541f8c32abf63`. The actual committed-head
producer build succeeds, its managed unit is inactive with MainPID zero and
successful exit, and its 33-record native payload passes composition/layout
gates. The native build directory continues existing outputs; this is not
claimed as another fresh complete native suite. Config and native ELF hashes
are the PR445 compatibility-profile identities documented above.
Producer coreboot config is
`d6008d778842c4040b2e8c1a72f65cd59002c0c46fc906888473e02991b8469a`;
committed default ROM is
`23c88b74427aca8445e8622f9cb9921b3cc2f2accf45396c968ba7d52fb738d4`.
Its actual QEMU runtime-services assertion passes Linux, variables, bounded
DMA denial, console replay and shutdown.

The same producer is separately paired through the strict normal tool with
the explicit fresh 36-record PR445 setup profile, retaining its native ELF
and config identities already documented. The paired ROM is
`4ec8606b65ca1a8009bd0e74139af65ee6319cca4691c08ea63c164a4537ca40`.
Actual F2/navigation/Escape/Linux continuation and the two-boot firmware-setup
request/presentation gates pass. This paired setup image is not described as
the committed default payload composition. An initial dirty-tree pair refusal
and preceding staged-index prototype are excluded as exact final-head proof.

An independent reviewer replays all three original-path runtime/setup oracles,
inspects initial/navigated/restored captures and verifies all 79 retained-file
checksums in a fresh extraction. The extracted runtime oracle passes; the
relocated firmware-setup chain retains its absolute fixture identity refusal.
No manifest is rewritten to convert that refusal into a pass.
These are compatibility-provider,
bounded-topology Q35 results, not protected-owner activation, hardware or final
project sign-off.

## Actual Linux BGRT table/image receipt

Concatenate `qemu-bgrt-346-445.tar.gz.part-aa` and `part-ab`. Archive SHA-256 is
`30859ae9d7482c1a176303e7b219dd34b0f49c0eb58d6166ee4e619e84c288c0`;
part hashes respectively are
`f0fe696adb6e6055807d052d5042135c23580d3abc5d9f0fb53ad8d1016016cd`
and `268267f12d25f41d9efd4015c6760afba6440280e23a3fe0742473695f65b156`.
Root verifies all 35 evidence-file checksums in a fresh extraction and checks
that TPM state, sockets, PIDs and private signing material are absent.

The test tools are the exact seven source files from signed CDK2 PR447
`80870fb2d5edeea4a97c3425d06835a71e9f3a39`, directly after PR446. The actual
firmware tested remains the earlier genuine coreboot PR346/CDK2 PR445 paired
ROM `d5e9f46d5f0c32de884fe00228d511ecc8ff5a1bd19d2f27c97e25585abc806d`.
This is not falsely relabeled as a PR447 firmware boot. The explicitly selected
Linux guest captures the real read-only BGRT sysfs table and image; default
fixtures are unchanged. Exact pinned kernel, busybox, cbmem and EFI-stub inputs,
opted-in UKI, derived NVMe disk, source-bound manifests and raw run are retained.

Root and independent peer execute the original-path actual oracle and causal
scratch-copy provenance/refusal suites. The 56-byte table has SHA-256
`67ebc6061178ae3c60f057ed856a7ecec2e6e9535bae866b417226346f9cda6b`.
Its ACPI signature/length/revision/checksum and BGRT version 1, displayed
status 1, BMP type 0, nonzero image address and (283,183) offsets validate;
the actual sysfs scalar fields agree. The entire 164,790-byte image is byte
identical to the configured BMP, SHA-256
`a05b76f687b176e39376a5fe2e1955cf6f5cd36435b9ef20ab66bb28d5f3909a`.
The oracle binds actual ROM and NVMe identities to the derived fixture and UKI
source manifests. CRLF is normalized explicitly; misplaced, duplicated and
reordered evidence, changed fields/checksum/image and changed provenance fail
at their specific assertions. Synthetic tests are unit-only, not QEMU proofs.

This closes actual BGRT field/image validation for the selected unrotated
Q35 logo profile. It is not every platform/rotation, a new protected-owner
activation, hardware, or final project sign-off. Absolute source/fixture
identities are retained; do not rewrite archived manifests to manufacture
an original-path replay from a relocated extraction.

## Exact PR349 producer with PR447 payload and tools

Concatenate `qemu-reviewed-349-447.tar.gz.part-aa`, `part-ab` and `part-ac`.
Archive SHA-256 is
`8800707d09003f4fbc64c437f7be52334b85dbd2f7c8b15c8213d586d1735fd3`.
Part hashes, in that order, are
`03f0076bc454a9993587773370a72734a92ed3b88d1c46540e7b06ebd68982cf`,
`152e9afbaeec19c307d36803664b37d0a6fec3bf142fdaa140f969cf3eaee835`,
and `498a09b6a6b9874880e9b3d87640518ffaed9c35550e60119565eca979747a6a`.
An independent fresh extraction verifies every evidence-file checksum;
TPM state, sockets and PIDs are excluded. The separately archived opted-in
BGRT fixture and UKI above remain unchanged.

The exact CDK2 firmware and test-tool source is signed PR447
`80870fb2d5edeea4a97c3425d06835a71e9f3a39`. The full native check succeeds
in the continuing PR434-origin build directory, not a fresh whole build.
The managed unit completes inactive with MainPID zero and successful exit.
Separate stable lint and native packaging succeed. Compatibility config
remains `6942c16419bb25e1ce8e760bf383bc7ffbcbc9fcf80f77c55e6a968f5baffe24`;
native ELF SHA-256 is
`10124df0ab90303fce3e26cb565a4478986febeee6a49a0583975e65f7c944e5`.

The separately fresh 36-record setup profile succeeds through strict native
MTRR, composition, mutant and ELF gates. Its configuration remains
`b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb`;
ELF SHA-256 is
`3f653e9c48a611ea774f73f897edf1f45cc785f9a31fff66d3e5f42d7cf2fc02`.
The unchanged committed PR349 producer is paired through the strict normal
tool with this explicit setup payload, yielding ROM
`30606448c786af956efe707a78265854d312433d39fc92367bbefa0b0e64d31b`.
This pairing does not change PR349's committed PR445 gitlink or imply that
its default 33-record composition includes these setup modules.

Actual QEMU runtime services, F2/navigation/Escape/Linux continuation,
and the complete persisted firmware-setup request/presentation sequence pass.
The real Linux BGRT table and entire BMP pass the actual oracle with the
same table/image hashes and offsets documented above. An independent reviewer
reruns all four original-path runtime, hotkey, firmware-setup and BGRT oracles,
and visually inspects the readable initial LVGL setup controls/help panel.
Raw logs, captures, provenance, configs, ELFs, paired ROM and completed native
check logs are retained. These are bounded-topology Q35 compatibility-provider
receipts, not protected-variable activation, hardware or final sign-off.

Later full-frame visual inspection finds a real limitation in these accepted
setup runs: the restored logo covers the middle of the still-visible form.
The historical setup oracle requires only 1,000 recovered splash pixels and
does not reject those remaining controls. Its recorded pass is preserved as
an original-oracle result, not clean whole-surface restoration or visual
sign-off. Strengthening that oracle and restoring the original framebuffer
after an actual UI visit remain open. The BGRT table/image receipt above is
unchanged; it does not assert the correctness of surrounding screen pixels.

## Reviewed private linked-primary loader, PR448

Concatenate `qemu-private-linked-primary-eee2.tar.gz.part-aa`, `part-ab` and
`part-ac`. Archive SHA-256 is
`3f8829478c1f316fe26afd0810c1fb841bd2cf509c927b271df3b1cbaf1d7a69`.
Part hashes, in order, are
`437562dea04c197c324c8a36ba74fceae82d2f8a837329287eb0baf83274de7a`,
`f1471bcc37d2f3bb7922694b84ceca19e975d493acc4ad0478fa9b651aee2d78`,
and `79f1d93b9b08ff3e03f48095e24609c439738155ff030f672d8264b114a206fb`.
An independent fresh extraction verifies all retained-file checksums and no
symlink or socket. Guest TPM state, PID files and private keys are excluded.

Signed CDK2 PR448 `eee2b511381ce0f45e71f33279775a9191e86173` continues
directly after PR447. The new private loader uses the real image transaction
owners and rejects protocol-notification attempts to execute a pending linked
image before post-load validation. Actual public/shared/raw StartImage reentry
tests, O0/O2 sanitizers, causal guard-removal mutants and real Make header
dependency queries pass independently. The initial whole-suite missing-header
refusal is retained separately from the corrected exact-head success. The
whole suite continues the PR434-origin build directory; separate stable lint
and native packaging pass, not a fresh independent whole build.

The unchanged committed coreboot PR349 producer is paired with the fresh
36-record setup payload. Resolved configuration SHA-256 remains
`b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb`;
native image SHA-256 is
`4e8aa3247ba37c698e41c203a5ea092c06e87ee6a6f0e4e8c13123f9aaf35390`;
paired ROM SHA-256 is
`dd982a4ed22b7b18d0b0ec3fefe7ed36d8a67790b7d9b488beb69070081af6e2`.
Runtime Linux/EFI services, F2/navigation/Escape, persisted firmware-setup
request/presentation and actual Linux BGRT table/full BMP pass independent
original-source-path oracles. Wrong-checkout canonical-path refusal is retained;
the earlier overwritten working-directory refusal is transcript-qualified only.
A separate incorrectly invoked strict composition gate correctly rejects the
default component profile's missing FMP; the genuine setup composition passes.

This does not update PR349's committed payload pin. These compatibility-provider
Q35 receipts do not establish clean whole-frame setup restoration, protected
variable activation, protected origin/lifetime closure, hardware or final sign-off.
