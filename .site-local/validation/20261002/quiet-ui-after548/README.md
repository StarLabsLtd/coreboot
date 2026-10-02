# Quiet protected setup acceptance after CDK2 PR 548

This additive packet records an actual passing three-VM quiet UI run and its
independent saved-output checks. It preserves the preceding failed run rather
than replacing its result. The evidence is bounded to the controlled, single-CPU
Q35/QEMU launch and the explicitly selected producer/consumer profiles.

## Result and scope

Author execution session **91966 actually reaped 0**. `passed-91966/native.status`
and `native.time` retain that outcome: GNU time **1:46.58 wall**, 122.45 seconds
user, 19.24 seconds system, peak 377380 KiB. This is the entire wrapper invocation,
including HOST work and three VMs, not a firmware startup timing.

The actual hotkey VM reached Selecting, real F2 entry, navigation, Escape return,
exact splash/status restoration, saved CBMEM provenance and Linux runtime
continuation. The request and presentation VMs exercised BootToFwUI persistence,
actual GUI presentation and Linux continuation. The full final store oracle
compared every byte of the 64 KiB variable region and all bytes outside that
region; no ignored-byte mask was used. Root independently replayed the saved
quiet hotkey/presentation validators (99799, actual 0) and full BootToFwUI oracle
from the exact worktree/canonical fixture path (8427, actual 0). Those were saved
evidence replays, **not fresh independent boots**.

The consumer deliberately selects `CDK2_NATIVE_QEMU_TEST_FMP=y` and ESRT1. Thus
this is quiet protected UI/variable/capsule-window integration evidence, **not an
actual SystemFmp frontend CHECK/SET or flash-update proof**. It makes no arbitrary
OS, other-board, physical-hardware, power-loss or general firmware-writer claim.
Serial/debug/SPI printing remained disabled in the quiet profile; observation used
the actual bounded CBMEM console through read-only QMP physical reads. Preparing
and Starting success text were source/HOST-covered in the preceding status
packet; the actual screenshots here specifically prove Selecting and usable UI.

## Exact source and immutable inputs

The run's clean signed tool worktree was
`/home/sean/Documents/.cdk2-worktrees/quiet-sata-topology-after544` at
`ff4792d4239ae38f75b7369a333d9dd337403be1` (PR 548), following geometry checkpoint
`4df470a89541dfcf96df69f86af707957b7ebfa9` (PR 547). The nine tested paths are
listed and hashed in `signed-source/nine-paths.sha256`; their signed blobs and
four source-rendering inputs are in the adjacent source archive. All nine
before-run hashes and actual caller config/header/ROM/tool hashes were checked
again after the run without drift.

The immutable prepared production build is
`/home/sean/cdk2-root-quiet-provider383.sggGBD`. It was built earlier by root
(74522, actual 0), from clean CDK2 source
`6afc95e8436708535ddbe6405dcce8d2b07371da`, not rebuilt at ff4792. Root supplied
that source binding; the retained pre-build manifest covers the config/header,
not an invented full pre-build source inventory. The prior native entry/rendering
bodies are archived separately. The new wrapper rebuilt the genuine HOST LVGL
reference, not the firmware ELF. No raw CBFS-extraction equality to the original
ELF is asserted; packaging reconstructs a payload representation.

| Input | SHA-256 |
| --- | --- |
| Production ELF `native/cdk2-coreboot-image.elf` | `f876be253985a8454a705573067163e2d82ad7dd3b053980ca879d7ed9e35682` |
| Packaged quiet ROM | `715a56bc9e2726ec15517bec3bfae2a22222b65aff4aec756661d582307981b0` |
| Prepared `.config` | `4232e9a9547717c2effceb4e525101b3814fe6edfee7439cbfb522273515865c` |
| Prepared config header | `5fee53cf846a45172a6b8ef714596508ece6e74f34de93ca2902c7cf3845c783` |
| Original new-trust producer ROM | `48114dbda96a7165339d94fccc00f3c1cdbe630e8bebeb5e7117853badc9cbc1` |
| Matching cbfstool | `2329a80e0691e751de79886482fc18cfd49866e79ab671ce73f24ec01ea6d0f9` |
| Producer full config | `affdbf37d1a94634ede4d162d325a30fe010036a3a8c6d566987b36b34cfde35` |

Producer source/codec worktree:
`/home/sean/Documents/.coreboot-worktrees/fmp-auth-initial-version-after382`,
signed clean `c7e8aaf6c917c55467e4f4f272aea048c23d518f`. These binaries are only
hash-referenced, not duplicated. Their public trust/build facts are already
recorded in the earlier auth/provider evidence. This packet contains no private
signing material.

The retained wrapper command in `native.time` used TMPDIR `/home/sean`, the above
actual COREBOOT_TREE and matching `CDK2_AB_CBFSTOOL`, stack limit 65536, and:

```text
bash util/qemu/bin/run-quiet-setup-acceptance.sh \
  /home/sean/cdk2-root-quiet-provider383.sggGBD/quiet-provider383.rom \
  /home/sean/quiet-provider-state-native-after548.KtpRc8/run \
  /home/sean/cdk2-root-quiet-provider383.sggGBD
```

## Geometry, observer and causal checks

The source C renderer computed the actual fallback logo rectangle
`[157,268,486,63]` and status rectangle `[157,339,486,48]` for the real 800x600
framebuffer. No screenshot bounding box or unchecked rectangle environment input
defined the exception. The reference manifest binds the real producer config,
source bodies, resolved config/header and raster. The saved validator checks
actual checksummed coreboot tables and framebuffer/splash/CBMEM extents. Once
the observer accepts a complete table chain it rejects any later valid-checksummed
chain SHA drift before replacing metadata or dereferencing a new console.

HOST raster execution 77483 actually reaped 0 under the genuine quiet prepared
config/header. Both the actual fallback above and the genuine old producer's
extracted 234x234 BMP passed. That BMP gave logo `[283,183,234,234]` and status
`[283,425,234,48]`; it is a HOST C-raster comparison, not a new BMP firmware boot.
Final 20 observer/geometry HOST tests passed. The exact accepted-chain guard
discard mutant failed the named table-drift test (exit 1), with reverse source
parity; this is a Python causal, not a compiled SIGABRT claim.

`actual-pixel-checks/replay.py` extracts ten existing mutation functions from
the reviewed, geometry-aware HOST selftest source and applies them to finite
copies of the **actual 91966** hotkey captures. Actual session 12063 reaped 0:
unchanged/one-pixel navigation, missing restoration, changed original status,
one outside-strip pixel, changed Selecting glyph/crop, blank reference and
hidden heading/controls each produced **exact exit 1 and the expected reason**.
Screen hashes were updated by the existing mutations so rejection was semantic,
not merely digest mismatch. The actual inputs and validators stayed unchanged.
Mutation source SHA is
`29f8ac5ab91fd555d6949da086f60d326f08b2d8f68c5a25c63f7c8c52d728c8`, signed
test-only leaf `8b5025c7334d8099d1aa51ed93c5e149f232d3d2` after ff4792.
Independent full HOST selftest 19304 also actually reaped 0 with source SHA
unchanged; it remains modeled evidence, not another native GUI proof.

Root visually inspected the actual `root-form-navigated.png` and
`root-selecting-stable.png`: the LVGL controls/focus, StarLabs wordmark and
Selecting status were visible. These are conversions of real captured frames,
not generated substitute screenshots. Visual observation does not override the
strict pixel/restoration checks.

## Whole-store correction and preserved failures

The selected real Q35 cold FMP authvar owner legitimately creates a canonical
20-byte all-zero `FmpState` variable, attributes 3, before the mandatory MTC
update. The GUID comes from the actual immutable CBFS config through real
coreboot `parse_uuid`/`hexstrtobin` implementations, not a duplicate Python parser
or hardcoded guessed value. All three extracted producer configs must agree.
The corrected expected transition includes that record **only when the actual
selected producer enables both owner/broker mechanisms**. MTC moves 1 to 2 and
OsIndications 0x11 to 0x10. All other canonical history, FTW header/queue/spare and
region bytes remain exact. Full HOST validation has 104 hostile changes in both
O0/O2; the two exact comparison-discard mutants are compiled in O2 only, each
targeted Assertion/SIGABRT 134 with strict sanitizer-fault rejection and reverse
whole-source parity. The outer `Aborted` lines are expected causal failures.

Independent final store replay 25701 actually reaped 0 on both actual new-profile
7810 captures (GNU 8.86 seconds) and historical genuine no-FMP f7 captures
(GNU 9.29 seconds). The latter imports signed old codec source
`636f67a94129a4606b05c35076054003a01cf9a5` and matching old cbfstool; its seven
store codec TUs equal the new producer's bodies. Neither replay is a fresh boot.
Omitting the required FMP record, using a wrong GUID, or adding it to the old
profile each refused. Exact source/import archives and checks are retained.

Preserved results are not relabeled:

- **7810 reaped 1**, at signed 4df: geometry/pixel/restoration checks passed,
  but the then-existing store oracle failed at region offset 364 because it
  expected MTC where the real FmpState record preceded it. All three VMs ran.
  The decoded actual record and both requested/consumed tuples are retained.
- **97511 reaped 1**: source identity drift during a peer replay, caused by the
  author's include/comment-only cleanup while that reader was active. Functional
  checks do not turn a failed identity gate into a pass. Both before/after hashes
  and the failed replay are preserved.
- The initial 18-case geometry HOST harness failed before its assertions were
  corrected; the final 20-case gate passed. The initial actual screenshot replay
  used historical hardcoded 234-mode mutations: it safely refused an outside-strip
  change, but failed the harness's expected-reason assertion. Only the final
  geometry-aware ten-case replay is a pass.
- Root's earlier 23904 real run failed its old fixed-width Selecting reference.
  Its raw outer log is included; the finite earlier packet
  `quiet-publication-geometry-after546` retains its images. Root's later wrong-CLI,
  wrong-fixture-path and debug-default-for-quiet saved-oracle invocations each
  failed honestly. Corrected exact-worktree/quiet invocations passed.

## Contents and omissions

`original-copies.tsv` binds each copied receipt to its surviving original path
and SHA; `files.sha256` covers this packet. The packet retains all three final
VM logs/configs/QMP action transcripts, CBMEM/table/raw-console/publication
provenance, reference rasters/geometry and relevant screenshots. Finite HOST
64 KiB initial/requested/consumed NV extracts are included for exact codec replay;
these contain the controlled test defaults/MTC/OsIndications/FmpState, not guest
disks or full pflash media. Mutant source/logs are retained, not compiled binaries.
There are no private keys, certificates with private material, guest disks,
full ROM copies, TPM state, sockets or duplicate build trees. Signed source
archives are finite selected paths, not entire checkouts. Complete original
artifacts remain at the paths in the receipt index.
