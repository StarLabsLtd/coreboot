# Q35 provider and native INFO/CLOSE: bounded checkpoint receipts

This additive packet binds signed coreboot provider checkpoint
`6b62fd5d7195e7a2313d286b4ac2d08298286cca` (twelve paths), signed CDK2 native
component checkpoint `c31e9dc817015a8ceec6fc05dc61f4c5033d8628` (four paths),
and its separate source-reference classification follow-up
`6afc95e8436708535ddbe6405dcce8d2b07371da` (two test scripts).
The native checkpoint's parent is `e22626169c7aa280e9f07ba0dd62545d5ed64b4f`.
Source archives and per-file hashes reproduce those exact signed blobs; they
were captured after testing, not represented as pre-build manifests.

## Actual results

| Receipt | Reaped result | Scope |
| --- | --- | --- |
| Author enabled build 96016 | 0 | Corrected incremental full graph after real SMM memchr closure; not a fresh author build |
| Independent enabled build 71315 | 0 | Genuine fresh full graph, separately owned output |
| Author SERVICE-disabled build 77537 | 0 | Separate genuine configure, then full build |
| Independent SERVICE-disabled build 68784 | 0 | Genuine fresh full graph, separately owned output |
| Author final native component 69159 | 0 | One cold VM, genuine protected config and enabled provider |
| Independent native component 2870 | 0 | Another independently launched cold VM using the independent enabled ROM/tool |
| Author final publication HOST replay | 0 | Recorded final formatted-source replay; modeled dependencies |
| Independent signed publication replay 09d753 | 0 | O0/O2 drift matrix and four targeted assertion-134 causals |
| Author importer/client source guards 43733/17956 | 0/0 | Exact classified reference lists plus O0/O2/ASan+UBSan tests |
| Independent importer/client guards 47988/14453 | 0/0 | Same exact final two-list follow-up |

Results above were reported by actual tool reaps, not inferred solely from log
PASS text. Elapsed times were not measured for these receipts; none are
invented. The author final publication replay status was supplied by its author.

SERVICE-disabled means only `Q35_SMM_CAPSULE_BROKER_SERVICE` is off: the existing
broker/FMP owner, auth policy and buffers remain selected. It is not broker-OFF
or a proof that the old owner is disabled. Author and peer enabled configs and
ROMs match, as do their SERVICE-disabled configs and ROMs. The enabled ROM is
SHA256 `1d9e2186c8200697f06695dc78a08d5abf07d41c491066cf958a4f663f4a4fd7`;
the disabled ROM is
`7e8f475a61d6aa0a9e4c04a7a159170fe834c92a417d6edfda57517bc8e67719`.
Enabled config SHA256 is
`0c8968622ced41d91ddfe7eada0da4916d885fae3d0f727c4ea50f135c8b686f`.

## Native observations and limits

The actual component imports the owned coreboot HOB and negotiated RAM-only
endpoint revision 2/generation/E8. Before lifecycle/Variable/PCI drivers, it
uses the real cache/barrier/OUTB client to issue READ_INFO, two actual CLOSE
RPCs, then READ_INFO after closure. Both native runs require all six markers:
BEGIN, actual RAM policy, endpoint RAM revision 2, READ_INFO, CLOSE twice and
post-close INFO. The client has no closed-state fast-success bypass for the
second CLOSE. Unexpected capsule HOBs cannot skip this new-mode probe.

Both use genuine Kconfig-resolved protected input, without READY bypass or
new-mode macro seeding. Config SHA256 is
`4232e9a9547717c2effceb4e525101b3814fe6edfee7439cbfb522273515865c`,
header SHA256 is
`5fee53cf846a45172a6b8ef714596508ece6e74f34de93ca2902c7cf3845c783`.
Both observer ELFs are SHA256
`90bd1177128227214989fa6e8281b43cc39efb9bfe4a6d39d502fe3649e61c2f`.
The independent native used its own fresh build directory
`/home/sean/q35-provider-info-close-peer.tkRf6E`, run `run.8Gk7Ws`;
the author's final run is `run.xQUlj7` under
`/home/sean/q35-provider-info-close-native.4OrgJ2`.

These are two separate one-cold-VM observations, not a same-media two-boot
recovery test or a production full-graph run. They prove actual endpoint,
metadata and denial-only CLOSE plumbing. They do not prove CHECK/SET execution,
an authenticated capsule apply, physical writing/readback/restore, disk update
lifecycle, hardware authority or whole-project closure. Those remain open.

## HOST publication and source qualifications

The publication fixture extracts the real sender readiness body and receiver
full-request completion guard. Its receipt/current/firmware/buffer dependencies
are explicitly HOST models, not actual SMM authority. O0/O2 positives cover
record placement, header/generation, buffers, firmware, receipt/current and
completion drift; every byte of the real private frame is checked for refusal.
Late-record and full-request guard discards each run at O0/O2, requiring the
targeted assertion and exit 134, rejecting sanitizer-only failures and
reverse-reconstructing the full production TU for exact comparison. Temporary
mutant binaries/logs are removed by the existing runner; their aggregate logs
and exact signed extraction/mutation script are preserved, not invented raw
per-mutant artifacts.

The independent enabled output's `production-source.sha256` is an AFTER-build
ten-production-body snapshot, checked unchanged across enabled/disabled builds.
No pre-build full-source manifest was recorded. Native and publication source
bytes were frozen for the independent tests and checked against the signed
blobs. The two exact reference-list additions classify only the native probe;
no parser/client guard was removed or broadened.

## Preserved failures and packet contents

`failures/` retains the genuine initial enabled SMM link failure (91589, missing
memchr), author combined olddefconfig/all NOCOMPILE refusal, independent wrong
OFF config-path/toolchain harness failure (14392), native missing LVGL before
compilation, old-policy-zero native assertion failure (50531), and publication
mutant compile failure for an unused frame. None is relabeled as a pass. The
independent wrong-path output had an actual `full.config`; the packet's initial
copy mistakenly requested `off.config`, failed read-only, then was corrected
before manifest generation. It did not change any build result.

Root importer 84933 and client 15924 each returned 1 because the strict source
list did not yet classify the new probe; positive parser cases passed. Their
evidence is the recorded tool transcripts only: no local raw log exists and no
one is fabricated here. The later separate follow-up preserves both exact
guards and its author/independent raw logs are included.

`build-native-inputs.tar.zst` contains finite public resolved configs, four real
ROM/tool pairs, native ELF/maps/serial/QEMU logs and its own 26-entry manifest.
It excludes modified native guest pflash, NVMe/store disks, private keys,
certificate-generation logs, full scratch trees and duplicate Git history.
Three source archives contain exactly twelve/four/two signed paths. Outer
`SHA256SUMS` covers the copied archives, hashes and logs. Existing evidence
directories are not rewritten. Packaging copies remain recoverable at
`/home/sean/provider382-packet-input-recovery.Fez6qJ/inputs`.
