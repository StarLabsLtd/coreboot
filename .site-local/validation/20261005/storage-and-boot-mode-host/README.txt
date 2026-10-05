Bounded HOST evidence, 2026-10-05

Mechanical original-byte copies: 20 logs and 16 configuration inputs. Mapping:
pre-storage logs: /home/sean/storage-policy-host.xCoPhY/*.log
pre-storage/config: that directory's build/{.config,include/cdk2/config.h,
  kconfig/coreboot-source.tmp,kconfig/coreboot-input.identity}, flattened.
joined-storage logs/config: /home/sean/storage-policy-joined.aSImOF, same layout.
boot-mode logs: /home/sean/boot-mode-authority-host.g7tBNI/*.log
boot-mode/config-o2: that directory's build, same four flattened paths.
boot-mode/config-o0: that directory's build-o0, same four flattened paths.
No generated executable, old-source duplicate or build tree is included.

Observed Root execution results below are tool results, not persisted child
exit-status files. Empty logs are preserved; emptiness alone proves no success.

Storage prejoin: execution/configuration/configured-execution/gate returned 2
while configuring the harness, PATH and missing top-level target registration.
configuration contains an inner missing-tool 127. Corrected registered-gate
55492 returned 0. Signed original source 6df44722b77de95170a32ab6d42626e6c2d8105c
was transplanted without delta changes onto signed58598 as PR690:
9cfd330eb9dc664975df98a6baaed76774488d82. Five-path full-delta SHA256:
60497dd2b76da468d0c4182978caec8d29a049e93bec722d3222c201e6c73850.
Joined configure269e70, mixed50959 and lint/native-stage67171 returned 0.
The mixed target runs existing partition/mutation models, all eight genuine
NVMe/USB/ATA Kconfig combinations and causal old-source cases 011/110. Inner
matrix configs and old logs were trap-cleaned temporary files, not independently
retained inputs here. Old causal refusals are expected failures, not acceptance.

Boot mode PR691 signed f6c3ea13efa482c57c659da86c52e0b689cbc36c, parent9cfd:
two-path full-delta SHA256
8a1e58452df436a9c768e7ee29610537739bd07bcc7b431e7ae9086b3bb98452.
configure d42b22: 0; initial positive34829: 0 (earlier fixture revision, not final).
Final O2 public model20857: 0. First O0 attempt929ad5: 2, correctly rejected a
divergent unconfigured build-directory override. Separate O0 configurec6b9ad: 0;
final configured O0 model92921: 0. Both final models use ASan/UBSan. Config headers
were byte equal. Old compile22875: 0; old fixture ed7050: raw1, twelve explicit
model failures and no sanitizer diagnostic. Four old-code acceptance cases
causally fail; short-first and unsupported-single already refused historically.
The old build substitutes only signed9cfd coreboot_hobs.c in the emitted real
17-source compiler recipe; final fixtures and sixteen other sources remain.
Shared compiler -MF output describes the last translation unit, NOT complete
17-TU/header closure. No complete tool/environment/input attestation is claimed.
Owned full-file C style72053, allowlist33068, native-stage976ea3 and public
native-coreboot-stage19536 all returned 0. Native-stage alone is not a VM boot.

Producer source retained at signed3e2609c8938a06c6bb782474c7a01209745a4319;
mbedTLS source retained at 0bebf8b8 (exact pin remains in retained build inputs).
No QEMU run, ROM flash, hardware test or full-project completion is implied.
The failing setup history is retained, not reclassified as a passing test.
