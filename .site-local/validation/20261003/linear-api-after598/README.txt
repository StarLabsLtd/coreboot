Linear native API types and exact source selectors — ready PR598
=============================================================

This finite HOST/compiler packet records the reviewed 17-path type-only
change, not a fresh Core build, QEMU boot, firmware installation, hardware
result, 32-bit ABI claim, or whole-project completion.

Signed candidate: 15c3bc1109ab39f06d1298895fdd95ae0dec6759, parent ready597
36d58d744f87d844b1ed7599717d06569dbac9a8. The preserved WIP 5cb47850c9
was tested on ready596; final 17 bodies are identical after the linear
restack. source/baseline-identity.txt names the exact signed596 baseline.
source/changed-paths.txt lists all 17 paths. Both changed-body archives are
Git exports, not dirty post-run source guesses. The third archive contains
unchanged include/fixture dependencies for the compiler proof, not history.

Seven C/H/test paths convert plain native phase APIs/callbacks to char,
uint64_t, uint32_t, size_t and unsigned-byte uint8_t storage. Ten exact
source selectors follow those declarations. The packed 1112-byte state HOB,
true EFI/MS callbacks and external HOST hooks remain unchanged. Native
UINTN/UINT64 aliases need not be language-identical to size_t/uint64_t;
all claims below are supported x64/-m64 comparisons, not generic 32-bit.
No _Bool conversion, parser inlining or protocol-ownership closure occurred.
The one-use prefix_mask helper remains in this candidate; its later
functional cleanup is deliberately outside this packet.

Proof and profile identities
----------------------------
proof/parity-test.sh compares 63 complete objects at O0/O2/Os: three
production consumers plus eight HOST fixtures, across actual P0/P1 headers.
The certified-refusal fixture is selected-P1-only, so P0 omits that fixture:
10 P0 and 11 P1 objects, each at three optimization levels. Every supported
whole-object pair passed. Six initialized representation/layout pairs also
passed. Source before-manifests, scripts and current helper C are retained.

profiles/*/proof.config and include/cdk2/config.h are the genuine copied
headers used for these comparisons; resolved.config and named-config.h are
the actual later normal Make resolutions. P0 identities are identical.
P1 proof retains ROUTE_ATTESTED=1 / PROTECTED_VARIABLE_RUNTIME=1 and FVB/FTW=0;
the normal named run without COREBOOT_CONFIG resolves route/protected-runtime
to 0 and FVB/FTW to 1. Both keep LINEAR=1, SystemFmp=1, TestFmp=0, LVGL=1.
They are not falsely described as identical producer profiles.

The representation helper reports full HOB/native size/alignment/offsets,
initialized padding, high64 status/clock/elapsed/report traces, all 256 byte
flag/required values, actual GUID HOB import/update and real boot-path setter
bytes. *.bytes are non-executable modeled native/HOB output, not flash media.
Enhanced final boot-path coverage was replayed separately after the first
object run; initial wrong loader size and corrected receipts are both kept.
Four O0/O2 strict ASAN leak/UBSAN lanes compare actual bytes/layout and run
the existing linear fixture with current RAM-retirement controls.

Actual receipts (outer process status, not marker-only inference)
---------------------------------------------------------------
50028 parity-corrected: 0; GNU 1:57.98 (113.85 user / 4.10 sys).
84252 enhanced bytes-corrected: 0; GNU 2.66 seconds.
15771 sanitized: 0; GNU 5.06 seconds.
34481 supported P0 five normal families: 0; GNU 5:23.91.
89060 selected P1 six normal families: 0; GNU 7:19.93.
47626 full linear HOST selftest: 0; GNU 3.60 seconds.
Final-parent corrected pinned native-linear-boot-test: 0.
Default project patch check: 0 errors / 0 warnings, 840 lines.
Root independently read proof/source and compared the object/byte scope;
xhigh independently read all seven type paths and ten selector changes.

The full selftest used explicit read-only pinned existing NVMe/derived disk
inputs, verified before/after (proof/selftest-fixture-inputs.sha256). No disk
is archived here and no seed, trust key or native producer was fabricated.
The display contract replaces two obsolete executable-FV/battery phrases
with actual direct-provenance inputs and exact gated EC GUID-to-PE mapping;
existing negative FFS/display/module assertions remain. This is source/HOST
contract evidence, not semantic battery-driver execution proof.

Preserved failures, not successful firmware results
-------------------------------------------------
42088 parity-p0-certified-unsupported: 1, baseline compiler failure from
including a selected-P1-only fixture under P0; corrected omission explicit.
Enhanced initial bytes-loader-size-wrong: 134, baseline assertion from a
fixture image-size smaller than the actual 512-byte minimum; corrected4096.
21671 P0 families: 2, selected-only certified-refusal config guard after
other positives; corrected supported five-family run above.
contracts: 2, unexposed outer target name. contracts-direct: 1 at full
selftest missing pinned disk after preceding direct source scripts passed.
display baseline and selector traces retain genuine obsolete-contract
failures; display-corrected is 0. Final-parent first linear invocation was
2 before build because LVGL override was omitted; pinned retry was 0.
Raw logs/times remain literal; no failed wrapper is relabeled a pass.

Reconstruction
--------------
Check SHA256SUMS from this directory. Decompress source archives and compare
regular members against their named signed Git trees. To repeat the parity
proof, reconstruct baseline source and candidate from those signed refs,
copy actual profiles into the script's p0/p1 include layout, and pass a new
output root plus candidate root to parity-test.sh. bytes-test.sh and
sanitized-test.sh use the same explicit output/candidate parameters.
Named-family logs retain the real compiler commands and generated artifact
paths; read-only BSSL/LVGL donors are dependencies, not archived libraries.
Binary objects, executable probes, disks, ROMs, keys and whole Git history
are excluded. Original immutable proof directories remain external.
