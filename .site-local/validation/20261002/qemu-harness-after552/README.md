# Wider QEMU harness receipt after ready 552

This is a finite HOST/harness receipt, not a fresh protected GUI boot, real
SystemFmp CHECK/SET, completed firmware installation, or hardware validation.
Most firmware logs, framebuffer samples, capsule/reset histories, and requester
traces used by the selftests are deliberately modeled input to real validators.
The BootToFwUI HOST fixture uses the genuine producer cbfstool to create and
extract its explicit legacy CBFS configuration; its variable store and console
remain modeled. Pass labels in these logs do not turn those models into native
authority.

## Actual runs

The root agent ran `util/qemu/bin/selftest.sh` from
`/home/sean/Documents/cdk2` at signed commit
`e77ac7cd5c2388df2042cfbdb88df91a8e968053`, with
`TMPDIR=/home/sean` and explicit
`CDK2_AB_CBFSTOOL=/home/sean/q35-auth-provider-newtrust-baseline.8ELcTU/build/util/cbfstool/cbfstool`.
The actual execution session 4675 was reaped with exit 0. Its unmodified raw
log and GNU-time output are retained in `receipts/after552.*`. The time file
measures this whole harness invocation: 4:58.67 elapsed, 207.49 seconds user,
69.99 seconds system, 414420 KiB maximum resident memory. There is no invented
per-case or independent-peer elapsed measurement. The raw log ends with
`harness self-tests: PASS`; successful termination is the root's actual tool
reap, not an inference from that string alone.

Earlier session 80442 after ready 549 was reaped with exit 1. Its complete raw
log and GNU-time output are retained as `receipts/after549.*`: the older
BootToFwUI oracle selftest stopped at the newly required genuine producer-tool
binding (`actual producer cbfstool is required for store-profile binding`).
That failed invocation is not relabeled as a pass. Its time file reports
1:07.40 elapsed and explicitly records nonzero status 1.

The final harness exercises fixture generation/reproducibility, payload
provenance hostile cases, deadline/descendant cleanup, modeled linear/setup/
BootToFwUI/requester/TPM oracle positives and negatives, configured acceptance
profiles, direct-image source inventory, and private-key exclusion. Messages
such as deliberate payload instability, `Terminated`, and missing FAT files
are expected negative fixtures; the full raw log preserves them. The script
generates a temporary wrong signing key solely to require rejection. No key,
guest disk, pflash, synthetic store, socket, TPM state, or large temporary build
tree is included in this packet.

## Source and generated-fixture qualification

The checkout's only tracked post-run change is
`util/qemu/fixtures/capsule-ram.manifest`: its `entry_sha256` was regenerated
from old tracked
`7b132610591f3c9fed5568968cad4fea8a051fa7cbcbc179043e403324f1ea3b`
to actual current fixture source
`49c980a91c129ebb4845fe4fe429af20b3f2c132d51ebbc90501c9a2ace536ae`.
The actual `util/qemu/fixtures/ram-capsule/entry.c` bytes have that latter hash.
The exact signed manifest, generated manifest, and one-line diff are retained;
the generated manifest is not represented as an e77 Git blob or committed here.

`source/runner-tool-profile-fixture-sources.tar.zst` is a finite source snapshot
from that exact signed commit: QEMU scripts/profiles, source fixture files,
selected inventory/build entrypoints, and their path/blob/SHA inventory.
Every archived path is compared with both its signed Git blob and the current
checkout. This is not an archive of the entire build dependency closure. The
source and tool checks in `provenance/` were captured after the completed run;
no immutable pre-run source or caller-profile manifest is invented. Temporary
directories named in the raw log were removed by the harness cleanup, so their
per-case discarded output files and scratch config headers are not available
in this receipt.

The real cbfstool is hash-bound here as
`2329a80e0691e751de79886482fc18cfd49866e79ab671ce73f24ec01ea6d0f9`;
the immutable producer/public-trust facts are already retained in the preceding
auth-initial-version-after383 receipt. This packet does not duplicate the
producer ROM, tool binary, private signing inputs, or build tree.

No additional native GUI or firmware-update execution was performed by the
packet reviewer. Root's actual whole-harness exit and raw evidence are distinct
from the prior independently accepted modeled selftest leaves and from the
separate quiet-ui-after548 native receipt.
