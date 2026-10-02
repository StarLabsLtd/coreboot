# Joined new-public-trust provider: metadata-only native receipt

This additive packet preserves actual native receipt 32668, result 0, using
clean signed CDK2 `6afc95e8436708535ddbe6405dcce8d2b07371da` in the separate
`q35-auth-provider-metadata-after383` worktree. Its source archive reproduces
the four native checkpoint paths and two exact source-classification scripts
at that signed tree. Archives are after-testing snapshots; no pre-run source
manifest was recorded or invented. The retained after-run working diff is empty.

The actual producer is the fresh enabled baseline-version/new-public-trust
build 48535 preserved in `auth-initial-version-after383`: ROM SHA256
`48114dbda96a7165339d94fccc00f3c1cdbe630e8bebeb5e7117853badc9cbc1`, matching
cbfstool `2329a80e0691e751de79886482fc18cfd49866e79ab671ce73f24ec01ea6d0f9`.
That earlier packet contains the genuine resolved producer config, public
PEM/DER and actual ROM/tool. This packet does not duplicate its ROM or key
history. Provider SERVICE and auth policy are genuinely enabled; FW_INFO
current/floor are both `0x001a0009`.

The ordinary named native component gate performs one cold VM: owned HOB and
RAM-revision-2 endpoint import, actual READ_INFO, two real CLOSE RPCs and
READ_INFO after CLOSE, all before lifecycle/Variable/PCI drivers. The actual
serial log contains exactly one BEGIN and all five required provider markers.
There is no CHECK/SET, signature admission, capsule apply, flash write/readback/
restore, same-media recovery pair, production full-graph or hardware claim.
The new trust certificate is packaged into the producer, but this metadata
test never authenticates a capsule with it.

Successful 32668 has managed duration 11.775 s; its raw GNU timing is 11.73 s,
user 10.50 s/system 1.23 s. Reaped status comes from the author's actual tool
receipt, not PASS text alone. The final run is
`/home/sean/q35-auth-provider-newtrust-native-info-clean.cLn7qs/native/capsule-provider-info-close-component/run.dsrfQ4`.
Observer ELF SHA256 is
`90bd1177128227214989fa6e8281b43cc39efb9bfe4a6d39d502fe3649e61c2f`.
Actual caller config/header before and after hashes match:
`4232e9a9547717c2effceb4e525101b3814fe6edfee7439cbfb522273515865c` and
`5fee53cf846a45172a6b8ef714596508ece6e74f34de93ca2902c7cf3845c783`.
The two recorded manifests and their independent current-file checks are kept.

## Preserved failures

The first dependency launch returned 2 before compiler/VM because its original
worktree lacked LVGL. Its raw GNU duration was 0.24 s (managed 272 ms reported
by author); its source was the earlier draft worktree, not the clean signed
source claimed for the successful run.

Clean-source aggregate 3238 returned 1: the real named Make/component VM had
already passed (GNU 11.01 s), but the subsequent outer check looked for the
nonexistent `CONFIG_CDK2_COREBOOT_AUTHVAR_ENDPOINT_ATTESTED` macro. That failing
outer check is established by the recorded tool receipt, not contained in the
Make-only log. `failures/wrong-post-run-macro.log` and the first VM's genuine
serial/QEMU/config files preserve its successful subcommand without relabeling
the overall result. The later 32668 repeats the genuine component on clean
source with the correct actual config assertion; no firmware fix was made.

During packaging, a direct comparison of the compact CBFS `config` record to
the full resolved Kconfig text failed because coreboot stores its own compact
representation. Both original artifacts are preserved in their respective
packets. Extracting `config` from the actual baseline donor ROM then compares
byte-for-byte with the native runner's extracted `coreboot.config` (SHA256
`48bf862bf36196c69a49ff6d06de4199eeed616e4e6184e12c6ca33e3ded7b65`). No config
normalization or weakened assertion was used to claim literal equality.

This finite packet contains selected raw logs, public config/header, native
ELF/map, actual serial/QEMU observations and exact source archives. It excludes
modified guest pflash, NVMe/store disks, private signing keys, combined signer
PEMs, key-generation logs and full scratch trees. Existing receipts and
historical failures remain unchanged; `SHA256SUMS` covers every copied file.
