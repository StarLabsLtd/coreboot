# Recovery checkpoint for nine obsolete coreboot worktrees

This archive precedes any removal. It preserves the exact ignored configurations
and all seven old ROM files found in the nine audited trees. It does not claim
that these historical ROMs were newly built, booted, or hardware validated.

Archive: `coreboot-retired-authvar-nine.tar.gz`

SHA-256: `7cb7084cba31ad244ee19ac3ee825e5267e434946e97a04f07b8f69dea75f329`

All paths are relative to `/home/sean/Documents/.coreboot-worktrees`. The nine
source checkpoints match their live GitHub branch heads exactly. Their tracked
files and initialized submodules were clean, and no initialized submodule HEAD
had commits outside its remote refs. The eight nested SeaBIOS repositories were
also clean with no commits outside remote refs, at
`b52ca86e094d19b58e2304417787e96b940e39c6`. No crossgcc donor resides in this batch.
Root, MTL and revocation lanes have cleared active reader/borrower dependencies.

| Worktree | Exact source checkpoint |
| --- | --- |
| authvar-certdb-candidate | 16703029307e0828046a91a7d0449e65b32a3fa7 |
| authvar-coordinator | 30752e84cfcc1deedf04cb40f744b2c6f905f446 |
| authvar-default-recovery | 7bc00eff9fa99c7ab8b28492404900b940b33f12 |
| authvar-default-store | 9c9f4cc18b894074b740e372437b83177e0cbd46 |
| authvar-mor-consumer | e648de322cc82b9feb304c7fec91401b0dc7e580 |
| authvar-mor-policy | d7dcc62b9c44ecdc82d4e2030f486b49d870446f |
| authvar-native-ordinary-set | eca4017899efaecb63e7969b150a20e6e439dc44 |
| authvar-policy-specific | f2880aeaae89bb0119cf4418b9ccc2787fb6b78e |
| authvar-presence-abi | 743377cf8c4822af6155d2977d41171db53c48cd |

The archive has exactly the 17 members listed below. `authvar-presence-abi` has
no ignored configuration or ROM to archive. `authvar-default-store` has only
its configuration. The ignored-file audit found no unique helper/source script
outside derived build outputs or the clean upstream SeaBIOS checkout, and no
ignored UART, CBMEM or receipt logs. Tracked fixture logs remain in Git. Derived
compiler objects and generated build files are not preserved by this archive.
All dirty review trees, active trees, toolchain donors and the evidence tree
remain outside this retirement batch.

```text
7bcec3fe4e7c24d15e80acb597797b60c76e1832fc84fb5b6e99f3ae5ff4fdf2  authvar-certdb-candidate/.config
fa8e64eaf32d1f7e083757e30e5dfb90eebd84ad39df34a120b0518df54953f8  authvar-certdb-candidate/.config.old
9bd4715e0bc9f5a641e71840b73b65e504dba5d8585ec082a54ac1ca0a02edbe  authvar-certdb-candidate/build/coreboot.rom
fa8e64eaf32d1f7e083757e30e5dfb90eebd84ad39df34a120b0518df54953f8  authvar-coordinator/.config
cf5d9f1a8c24b27f0aa04b002bee81baffd05b19fc5199d3fdb1364b02517cf8  authvar-coordinator/build/coreboot.rom
c5d614742f562c555d7d585bd713da0e65445ada50c46c65a57f4dd8777de17c  authvar-default-recovery/.config
addc2459b658752dee080bb1163cd7393632fd0e2cfab62aedf0f2d4544c97d3  authvar-default-recovery/build/default-recovery-q35/coreboot.rom
ca5cc218b9da3db84808d001c501a826c5726c5600a3ee55f7e9c2fc3216f41a  authvar-default-store/.config
6dd6947b94e4cc92d83d389ac354afad40f12ed6ccbb14a904cad387ae5e52ae  authvar-mor-consumer/.config
60d344d41f5bd95c98700adbf66fddf966d31089653f1ded739d61fa16e7e559  authvar-mor-consumer/build/coreboot.rom
6dd6947b94e4cc92d83d389ac354afad40f12ed6ccbb14a904cad387ae5e52ae  authvar-mor-policy/.config
f888408b2257af7bb190674ba21de3616d8aeb9301fb25df35ba6c1e32ba53de  authvar-mor-policy/build/coreboot.rom
03cad792baf792ac232dc32f398d7c2f388cdb77671f25340052be1e7c4c9d3d  authvar-native-ordinary-set/.config
ed212d782a40c912d732b78d0e3112af06d3c53c0abeac896f8bf49efba7a1c7  authvar-native-ordinary-set/build/coreboot.rom
6dd6947b94e4cc92d83d389ac354afad40f12ed6ccbb14a904cad387ae5e52ae  authvar-policy-specific/.config
373fece99c40de9efcf85ac4230a35ee03e15119cb1bdc488c0a7ac629464be9  authvar-policy-specific/.config.old
22149d9ac129605f6ae69e5d4b43469e27bfa0abd793b27368dcff91f2a4e7da  authvar-policy-specific/build/coreboot.rom
```

Recovery uses the exact source checkpoint from Git/GitHub, followed by archive
extraction into that worktree's matching relative path. Restore verification
must compare all 17 member hashes and each restored configuration byte-for-byte;
it does not require or imply that a historical build reproduces today's output.
