# Canonical capsule read admission, real warm reset, and RAM population

This packet preserves bounded QEMU evidence, not hardware or firmware-install
sign-off. No private PKI inputs are included.

## Source checkpoints

- Coreboot read admission: `cfbe73cc66e81e4852789c8df7fbcddca881442a`,
  parent `636f67a94129a4606b05c35076054003a01cf9a5`.
- Coreboot live RAM retention publication: `b6779f0fa0882aff2daa0149f4f70a8e5a3ce8fe`,
  parent `cfbe73cc66e81e4852789c8df7fbcddca881442a`.
- CDK2 pointer/reset mechanics: `dad47b9e87515a3fd119855a107adb282a2bac90`.
- CDK2 real UpdateCapsule/population variant:
  `b6fd74a8382ea31c5e900b2ad342f9fe42eeff2f`, parent `dad47b9e87515a3fd119855a107adb282a2bac90`.

All four checkpoints were signed and pushed. The copied final source files are
from the two latest signed checkpoints. The reset author's first named-wrapper
artifact predates the additive Update variant; the peer reset artifact is an
independent earlier mechanics build. The old mechanics was also rerun on the
final source successfully (session 29835), without enabling RAM delivery.

## Producer boundary and build

The canonical reader owns one bounded CBMEM snapshot and index of the actual
declared variable-store region. The genuine FV/FTW planner must report CLEAN;
pending/corrupt journals are refused without repairing flash outside MM. Both
variable lookups use the same snapshot and canonical winner, with exact
attributes 7 and 64-bit pointer schema. The temporary snapshot is scrubbed and
released through the real CBMEM API, with removal failure not described as a
successful release.

The Q35 retention provider is tied to actual same-header completed public-MM
publication, canonical read admission, RAM handoff, non-S3 boot and absence of a
live DRAM-clear request. The default generic provider remains false. The policy
record is emitted after the board's real service records; no writer authority or
extra test-receipt flag is synthesized.

The selected fresh normal producer build (session 99979) actually returned 0:
75.858 seconds wall, 151.518 seconds CPU, peak 340.4 MiB. Its command, output and
timing are in `producer/build.log`. It used the pinned existing MbedTLS gitlink
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`, actual referenced vboot
`5c360ef458b0a013d8a6d47724bb0fffb5accbcf` donor, `UPDATED_SUBMODULES=1` for those explicitly supplied inputs,
and `TMPDIR=/home/sean`; it did not reset the thirteen unrelated submodules.

The donor ROM is `355e3cdaeac2678c0ca6beb59a0a6a1df64a36f430e97b902b84d708adc6370c`.
Its matching tool is `1aa83b654c423ef23357dc0404d2d9b6b54a4be810a9ae5835a435743435efda`.
Resolved producer config is `8054f477472d7a6ac1eaf567c39cf41b9b1fd7f839e0ff04559b5bd5038b9ea1`.
The prior RAM-disabled mechanical donor is separately preserved as
`08432a78c509827f55fd596fd5b9001a26479f4b16846e25e461ea12f26268c8`.

## Actual reset mechanics

The original component uses genuine imported RAM, Core AllocatePages, real MM
SetVariable of CapsuleUpdateData under its actual vendor GUID, and the installed
ResetSystem(EfiResetWarm). No host reset, direct CF9 substitute, seeded flash,
synthetic memory descriptor or extra protected-boot guard discard is used.
The same VM restarts; coreboot discovers and coalesces the real SG capsule,
publishes LB_CAPSULE, and CDK2 imports and compares the actual 64-byte capsule HOB.
Valid owned delivery policy must remain disabled on both boots.

- Author 74651: actual 0, 11.080 seconds (corrected vendor GUID).
- Independent 78883: actual 0, absolute fresh-output configuration.
- Normal named wrapper 43254: actual 0, 10.896 seconds; archived author artifact
  `native-reset-author` is from this wrapper.
- Final-source old-mechanics regression 29835: actual 0, 11.579 seconds.

The retained first peer configuration failure was a relative CDK2_CONFIG path
rewritten after Make changed directory, not a guest reset failure. The corrected
peer uses absolute config/output paths and matches the actual resolved config.

## Actual UpdateCapsule and next-boot population

The new variant compiles the real CapsuleRuntime entry, runtime model and ABI.
It requires the actual RAM-only imported policy on both boots. Query uses the
installed runtime service; UpdateCapsule receives a real core-allocated SG and
an opaque 64-byte capsule with the recognized Windows UX GUID and
PERSIST|POPULATE|INITIATE_RESET flags. UpdateCapsule itself writes the pointer via
MM and invokes the installed real warm reset. There is no manual SetVariable or
direct reset in this variant.

On the second boot the unique real capsule HOB is compared byte-for-byte. A real
Core BootServicesData allocation stages those already validated bytes and the
terminated SG. The existing boot protocol's process_scatter then creates one
real configuration-table entry with exact capsule count and bytes. This stages
the component in honestly mapped Core memory; the production RAM-map validator
is not widened and no descriptor is fabricated for the entry32 stack.

- Author session 97215: actual 0, 11.789 seconds wall, 11.700 seconds CPU,
  peak 106.1 MiB; artifact `native-update-author`.
- Independent session 43882: actual 0, 12.36 seconds wall, 12.29 seconds CPU;
  artifact `native-update-peer`.

Both runs require VM exit 3, exactly two BEGIN and real-policy markers, one Query,
one Update initiation, one imported HOB, one population PASS and no FAIL. The VM
uses real secure pflash with the documented long property syntax, one actual
CPU, real Q35 SMM service, actual NVMe/XHCI/EDU/VT-d topology, and no host trigger.
Each run modifies only its copied ROM; original donor artifacts stay unchanged.

This proves opaque UX capsule retention and deferred population transport.
It does not prove Windows UX image rendering/checksum interpretation, authenticated
FMP firmware installation, capsule-back to another firmware, arbitrary board
retention, general chipset admission or hardware compatibility. The existing
dormant typed FMP writer/broker still needs a genuine installed platform provider
and authenticated live-ROM/checkpoint contract before firmware-update parity can
be closed.

## Refusals and causal hosted gates

The canonical read runner independently passed session 74344: eighteen O0/O2
cases and eight exact-source causal negatives, with strict sanitizer rejection.
This covers pending/corrupt/refused canonical input as hosted parser evidence,
not native journal corruption/reset trials.

Final policy host author 89167 and independent 13793 both actually returned 0:
O0/O2 strict ASAN/UBSAN, weak-false fallback, transport combinations, limit compile
negatives and the exact persistence-guard discard assertion. The initial new-hook
host failure is retained: unused weak fallback arguments under Werror, corrected
with explicit void casts.

The Update variant's initial failures remain raw, not relabelled as passes:

- first 71 ms invocation: missing kconfig-conf in systemd PATH, before guest;
- 85542: real Query/Update/reset and second HOB passed, population composition failed;
- 46615: stage markers show real Core/Variable/Reset/CapsRuntime ready, failure at population;
- 66635: core-owned SG still had ReservedMemory type 0, correctly rejected by the
  real CapsuleRuntime RAM-map validation;
- 97215: corrected component BootServicesData type 4, real positive complete.

The older wrong-variable-GUID native attempts and missing compile declarations
are copied in the reset first/typefix/compiled logs. No aggregate full-project or
hardware pass is inferred from these focused gates.

## Independent source opposition

Style reviewer accepted the canonical reader ownership/cleanup, the six-path
live retention hook, the four-path actual UpdateCapsule component, actual
BootServicesData staging, final signed-body parity, hosted policy replay and
independent native replay. Reviews explicitly retained the no-firmware-install
scope and refused fabricated stack mappings or writer capability. Root separately
integrated the earlier mechanical checkpoint and ran its normal named wrapper.

`SHA256SUMS` identifies every copied artifact. No private keys, generated PKI
directories or raw NVMe disks are archived.
