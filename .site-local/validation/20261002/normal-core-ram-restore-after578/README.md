# Normal Core RAM authenticated original-content restore

Actual normal production-flow restore passed on the author and an independent
fresh replay. This is not a component payload or post-write observer replacement.
Both begin with a byte-exact owned copy of their own successful normal update
image (`136d2259c24eead60340e9e4edd009bffb7e2de5513836240f3b150278f2a6a3`).
The ordinary MAIN app verifies genuine prior running/history `0x001a000a`, stages
the full signed inverse capsule through installed Query/UpdateCapsule, and never
uses a direct ResetSystem success fallback. Actual guest resets expose three
strict saved epochs: running a, running a with retained b capsule, running 9.
Here `9`/`a`/`b` mean full `0x001a0009`/`0x001a000a`/`0x001a000b`.

The pristine original target COREBOOT bytes contain the complete production
normal Core before signing. Its compiled FW_INFO is 9, while authenticated outer
MSS1 attempt b records successful durable history b. The final ordinary MAIN
asserts running 9, durable history/last attempt b, floor 9, status 0 and all four
present flags. These numeric facts are actual bound source assertions, not
separately printed numeric UART fields. SetupMode1/SecureBoot0 is asserted, not
enrolled Secure Boot admission.

## Actual outcomes

| Receipt | Outer status | GNU wall/user/system seconds | QEMU seconds |
| --- | --- | --- | --- |
| author52037, `author-restore` | 0 | 150.28 / 146.80 / 11.28 | 147.513567333 |
| fresh peer95228, `peer-restore` | 0 | 150.86 / 147.54 / 11.25 | 147.670814997 |
| initial98342, `failed-reset-console-motion` | 2 | 34.17 / 33.59 / 2.25 | 31.463079432 |

Successful guest exit is 3 with two real RESET events and no result failure. The
predeclared complete three-boot QEMU budget remains 180 seconds; GNU aggregate
includes preflight/app compilation. Each successful run compares the entire
COREBOOT route to the signed original pristine target, excludes SECURE_PROBE and
the adjacent gap, and proves the private NVMe disk unchanged after its ordinary
MAIN app insertion. Authorized protected variable-store writes are not claimed
byte-invariant. Final whole media on both successful runs is
`34aaf990a3a041e463e50c5f5b787f029cd6f9989b78e4fc5a3d5275bba9332e`.

Normal epochs require RAM success/end before PCI; warm epoch permits no PCI.
CLOSE before warm success cold reset is reviewed real-source-backed inference,
not a separately observed CLOSE marker. Saved bounded RAM capsule bytes compare
exactly to the actual validated signed b input. No image/media/capsule/private
signer is copied into this packet; these external artifacts are hash-only.

## Exact binding and retained failure

Successful fixture source is signed
`9ca912d6fa093f9a90d6ffed46229c61bdcd0fd3` after PR577, with parameterized
restore source from PR576. The actual Core/inventory is truthfully cloned from
the previously built production consumer `ea1b13a7479e22bb5db908142197813ce21f940b`
and ELF `e9d1c3166e5e16b7949af58d393cabfbfcd2bdb56fecb1218155b20b6cc00622`;
there is no claim of a fresh Core rebuild at the fixture-only tip. Both actual
full Core inventories enforce SystemFmp1/TestFmp0, real protected variables and
P1. Producer is signed `b06f91d78a891e1d803ea48543bdf545231ef786`, with unchanged
configured public certificate. Pristine target is
`e03749ed2f91b2404cec9fe8598bd6e684b7d1347c76551594dfd2a8f27c035d`, signed
inverse capsule is `1504fe2e50d580186124910e432982b3c99c33c8cf8d426240f52c481436ed20`
(attempt b, floor9, count2, PERSIST/INIT_RESET).

All 17,104 original before/after run-bound source/tool/config/input hashes agree
on each successful run. The maps describe those actual frozen runs; they are not
a retrospective claim about later edited source. Complete saved console/table,
UART/QMP/result and timing receipts are copied, together with exact signed Git
source archives. Independent `restore-replay` is a read-only saved-receipt check,
not an additional VM.

The initial c325 source run failed as the first reset entered coreboot: all eight
live CBMEM console header reads moved before a new stable snapshot was accepted.
No completed restore is claimed for that run. Its raw failure and signed source
are retained. PR578 introduces a typed ValueError subtype solely for eight-read
motion and permits retry only after an observed reset with a previous saved
epoch, no accepted current metadata, and the original fixed ten-second reset
publication bound. Malformed tables/geometry, physical reads, accepted-current
drift and initial non-reset errors remain fatal. No guest/policy/deadline change
was needed; successful retry uses the exact same immutable inverse inputs.

Native negative cases, DISK capability, final-tree regression, hardware and
power-loss durability remain separate work.
