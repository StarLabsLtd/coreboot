# Protected-variable seven-VM regression after ready 557

This is a real, unmodified native graph regression on the controlled single-CPU
Q35/QEMU profile, not a production SystemFmp firmware-write, complete EFI-loader
matrix, physical-hardware, or whole-project pass. The selected payload profile
still enables `CDK2_NATIVE_QEMU_TEST_FMP`. The separately owned authenticated
capsule writer was not invoked by this run.

## Actual invocation and result

Execution 96107 actually reaped 0. The exact command is retained in
`receipts/run.sh`, with the raw `native.log`, `native.time`, and `outer.status`.
The normal named target was
`native-protected-variable-fullgraph-cold-enrolled-test`, from an isolated
detached checkout of signed consumer
`ab2cb8fc9698c5f4fe901be4b541b13c49d816e2` (ready 557):

`/home/sean/protected-variable-seven-after557.sM1orT/source`

Its independently owned build/media directory was
`/home/sean/protected-variable-seven-after557.sM1orT/artifacts`.
The caller was configured by actual `make defconfig`, using the tracked
`tests/protected_variable_fullgraph.config` and genuine producer import.
Caller configuration/header paths, Kconfig and defconfig were all explicit
absolute paths. The runner then configured and built its own source copy
through the same real Make/Kconfig graph; no generated guard discard or
inventory stub was used.

GNU time measures that named target, including its inner native build, seven
VMs and HOST oracles: 110.51 seconds elapsed, 96.16 user, 20.52 system, peak
216896 KiB, exit 0. It does not include the prior outer `defconfig` or later
packaging/checksum checks, and is not an individual firmware boot timing.
No independent seven-VM replay or elapsed measurement is invented here.

The completed run is
`native/protected-variable-fullgraph-cold-enrolled/run.I4cUS1`, with baseline
`baseline/run.kFuSsW`. All seven VMs use private pflash and disk copies. The
immutable donor originals and the active capsule writer's media were not
modified.

## Genuine producer and refusal controls

The positive donor is the signed exclusive-E8-scope producer
`b06f91d78a891e1d803ea48543bdf545231ef786` (ready coreboot 386), from
`/home/sean/Documents/.coreboot-worktrees/q35-exclusive-e8-dma-scope-after385`.
Actual input identities are retained before and after the run:

| Input | SHA256 |
| --- | --- |
| `/home/sean/q35-e8-scope-full.9sM2hH/full.config` | `affdbf37d1a94634ede4d162d325a30fe010036a3a8c6d566987b36b34cfde35` |
| positive `build/coreboot.rom` | `a151488a93a5f4909d620ee940f0be7d6a421e2378cdda5f0ef3c0efd1483484` |
| matching positive `build/util/cbfstool/cbfstool` | `810b009112fd12ea92ecc83cd072b07226db62665f71b2ea007f8ffe23d1d777` |
| genuine absent producer ROM | `66a85df72dc5e8c4b91a1c649fef1d6bec888a860ee143ce8d43ad8b412b0fe9` |
| genuine old GENERAL-without-pin ROM | `b293fe84da5c88d42e592c826d0df6d6632b85a9ef6201edccf30631efc31c45` |
| matching absent/old GENERAL tool | `e2ea132a5bd001e002c7991b28e87f4f16c1c1c6d62af57721dbd5948436b186` |

Both negative donors are the original separately built producers, not edited
tables or endpoint-stripped copies. The actual configs were extracted from
each donor with its matching tool. The positive requires the genuine public
owner, attested route, and state-predicate pin. The absent donor has the actual
service disabled; old GENERAL has the public endpoint but no predicate pin.

The identical production ELF in all seven VMs hashes to
`40db64b596dc371e2d1be57ed651a9081cb0e627e5409089a27035708799535d`.
Its ordinary, enrolled and cold test apps are separate actual PE images.

## Observed gates

1. The genuinely absent endpoint refuses VariableRuntimeDxe/VARIABLES_MIN
   with NOT_FOUND before app entry. The exact same ELF's return-loop QMP
   oracle observes RAX `0x800000000000000e`, CPL0, SMM0 and IF0. Its store
   before/after bytes are identical.
2. Old GENERAL permits real VariableRuntimeDxe initialization but required
   EsrtDxe/VARIABLES_MIN refuses UNSUPPORTED before the app. Its exact QMP
   oracle observes RAX `0x8000000000000003`. No whole-store unchanged claim
   is made for this case: real initialization may recover canonical media.
3. Ordinary app boot reaches all seven runtime markers: real Ready closure,
   EBS callbacks, nonidentity SetVirtualAddressMap, physical runtime aliases
   removed, and converted Get/Set with closed operation 7. Actual VM exit is 3.
4. Enrolled boot reaches all 18 markers. Enrollment deliberately occurs
   after Ready and before EBS, using the genuine initial SetupMode PK bypass;
   this is not mislabeled as a pre-Ready or initially enrolled parent boot.
   Actual PK/KEK/db Auth2 enrollment, SecureBoot mode readback, unsigned and
   wrong-signer child denial, real signed-child execution/return, and post-VA
   newer db Auth2 acceptance/replay/rogue denial all pass. Actual VM exit is 3.
5. Cold unsigned MAIN uses an exact copy of the first genuinely enrolled
   flash. Live Variable/Esrt/Monotonic/BootPolicy passes, then real OS_HANDOFF
   refuses SECURITY_VIOLATION before Ready/app. QMP RAX is
   `0x800000000000001a`, CPL0, SMM0 and IF0.
6. Cold valid rogue-signed MAIN has the same genuine enrolled starting flash
   and the same real authorization refusal. This is not malformed PE input.
7. Cold db-signed MAIN enters with no enrollment blobs or reenrollment. Its
   nine exact markers check persisted SetupMode0/SecureBoot1 and ordinary NV
   before writes, then again after actual EBS/nonidentity VA and removal of
   physical aliases. Actual VM exit is 3.

Both cold denied-image stores are checked at O0/O2 by a read-only HOST oracle
using the actual producer FV/scanner/record/store/writer code. The complete
64 KiB expected canonical transaction permits exactly the genuine mandatory
MTC high-counter increment 1 to 2, not a generic byte allowlist. PK, ordinary
NV sentinel, wrong MTC count and journal corruptions are refused. The
comparison-discard mutant reverses back to the exact source and must abort
134 at the specific `!matches` assertion with no sanitizer fault. Its expected
`Aborted` line remains in the successful raw aggregate; it is not a failed
VM or a suppressed sanitizer finding. The separate HOST guard-shape matrix
also preserves genuine refusal for missing strict/linear/native-variable/
Security2 shape terms and its exact scoped causes. These HOST mechanisms do
not substitute for native authorization.

## Immutable source and receipt boundaries

Before launch, 1570 tracked non-gitlink consumer files, 448 pinned BearSSL
files, caller config/header and the exact donor/tool/producer-codec inputs
were hashed. Every corresponding after check returns 0; consumer source
status is empty. The two gitlink identities are recorded separately. BearSSL
is the unchanged pinned `8ef7680081c61b486622f2d983c0d3d21e83caad` source;
the existing exact checked build-local EKU patch remains part of the actual
build. During initial checkout preparation its copied relative `.git` pointer
was invalid. That metadata pointer was moved outside the source before
configuration and hashing; no vendor source was changed or gate rerun hidden.
No failed native invocation preceded execution 96107.

The runner's copied source-before/source-after manifests and resolved actual
inner profile are also retained. Actual inner header checks require P1,
strict direct, linear, native VariableRuntime and Security2, attested service
and DMA handoff; FVB/FTW and the legacy SECURE_BOOT option remain off. Secure
Boot authorization evidence is actual state/readback, not that legacy flag.

`source/consumer-sources.tar.zst` contains 14 finite build, runner, app, oracle,
profile and Core-entry source files from the exact signed consumer.
`source/producer-codec-sources.tar.zst` contains the five actual linked codec
TUs and their source-owned internal headers from signed b06. Each archived
body compares to both its signed Git blob and the actual frozen checkout;
their inventories are in `provenance/`. These finite archives are not the
entire build closure. The producer's complete code tree was not given a new
pre-build manifest by this regression: exact signed clean source identity
and the specific linked codec before/after hashes are the retained evidence.
Host compiler/QEMU identities were captured after completion, not represented
as new pre-run fingerprints.

The raw logs, configs, refusal registers, build/oracle receipts, source/hash
manifests, public test certificates and 14 small public-fixture store slices
are copied byte-for-byte. Absolute paths in those original hash receipts
refer to retained original artifacts; `files.sha256` checks the finite copied
packet itself. No ROM, guest disk, full source-input archive, private key,
TPM state, socket, or build tree is archived. Signed-source refs and immutable
donor hashes allow inspection without duplicating those large/private inputs.

This does not establish the production SystemFmp/RAM delivery frontend,
GRUB/systemd-boot enrolled loader matrix, arbitrary OS runtime, physical
hardware, or later-head whole regression. The actual authenticated writer
and restore component receipts remain separately owned and scoped.
