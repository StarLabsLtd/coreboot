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
