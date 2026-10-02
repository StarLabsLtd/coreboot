# Finite owned style audit and four fixture corrections

The audit snapshots CDK2 `a09dd6bbf3c0ecb1c19bf347b1d607f31623d710`
plus the four exact source blobs from signed
`d7cbd61a4633ae9dfe6acd3c02e15b26277e8837`. The primary agent compares
all four to integrated `dc2b0c60f233f3e306d5fd577db93260e8fe1bc2` and
compares the complete before/after source hash lists with exit 0.

The finite collector covers 893 C/header/Kconfig paths: 14 hash-bound
imported provenance cases and 879 owned paths. Collector exit 0 means the
receipts were collected, not that all lint passed: 873 owned checker runs
return 0, six return 1 with three errors and 19 warnings. The residuals are
three public FAT single-element wire tails, 18 stable literal diagnostic
operation labels and one real userspace ioctl errno check. Public UEFI
layout/sizeof compatibility is intentionally retained; diagnostics are not
renamed or suppressed just to claim a clean checker result.

The fixture packet preserves strict helper O0/O2/Os sanitizer and failure
effect checks, actual namespace and generic Core tests, the targeted NVMe
failure mutation and retained initial compiler failure. It is not a complete
compiler closure, protected authentication authority or hardware proof.

Root's integrated focused run is actually reaped with exit 0: unit
`cdk2-root-fixture-warning-after532-retry2`, invocation
`7028cb928d5b4cccb54c935a86130aa9`, 1m3.280s runtime,
1m54.833s CPU, 121.3M unit peak memory. Its log includes actual execution of
the TCG2 and Core binaries after building them, plus Security, namespace,
filter and exact allowlist gates. Both earlier wrong-target harness failures
are retained and are not counted as passes. The reviewed source is ready
PR533, following PR532 in the linear stack.

These receipts contain bounded HOST artifacts and raw logs, not complete
ROMs, mutable variable media or private signing material. No whole-project,
full protected-suite or final hardware sign-off is claimed.
