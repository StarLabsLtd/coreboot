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
