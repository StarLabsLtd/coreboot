# Authenticated Q35 component installation and content restoration

This packet records an actual first authenticated CHECK/SET and a subsequent
authenticated restoration of original firmware content. Both author runs and
both independent runs completed successfully using the existing normal named
component gate and its unchanged 90-second QEMU deadline. This is not a
production SystemFmp/Core RAM boot-flow, DISK update, hardware flash,
power-loss durability, cryptographic live-ROM attestation, or hardware
antirollback acceptance claim.

## Immutable source and configured authority

The producer is signed coreboot
`b06f91d78a891e1d803ea48543bdf545231ef786` (ready PR386), including the
exclusive held-E8 transaction scope after the lease-only erased-padding
optimization. The consumer is signed CDK2
`d3745376a42fc97f1ee5f95c84b9e49eda3c0dfd`, an earlier frozen component test
checkpoint, not the latest production consumer. The two source archives contain
exactly the eleven changed producer paths and six consumer fixture/recipe paths
from those commits. They contain no local signing key, guest medium, ROM or ELF.

The genuine selected producer was built with the explicitly configured HOST
fixture public certificate; its DER SHA256 is
`89f101575013365d7e553cc3ba123ba2ff8a785c25a945fddc53052c06a73bec`.
Production StarLabs consumer trust was not replaced. Actual MM authentication,
protected owner reconciliation, physical write policy, private pflash lease,
and current DMA checks perform admission; the component does not manufacture a
grant or seed version state. INFO/CLOSE do not reset controllers.

## Actual runs

| Run | Actual reaped result | GNU aggregate wall/user/system seconds | Artifact |
| --- | --- | --- | --- |
| Author first update, session42689 | 0 | 88.19 / 83.65 / 5.29 | `.hWEbI0/run.1d9YNy` |
| Author restore, session87353 | 0 | 87.65 / 83.27 / 5.24 | `.8oE1hv/run.ZOCQSR` |
| Independent first update, session80997 | 0 | 88.48 / 84.01 / 5.47 | `.gGvx8X/run.CG1qNq` |
| Independent restore, session16820 | 0 | 94.91 / 90.43 / 5.55 | `.S7Rphi/run.d3yrzM` |

These GNU timings include configuration, compilation, packaging, QEMU and HOST
readback. They are not per-boot or firmware-only timings. Each gate launches one
`timeout 90` QEMU process, within which the genuine cold reset boots the installed
oracle; the runner requires actual debug-exit status3. The independent restore's
94.91-second aggregate therefore does not change the QEMU deadline. Host load
was not controlled as a benchmark. Peer outer `native.log` files are empty;
actual managed child output is preserved as `native.journal.log`, copied here as
`peer-*.log`.

The first worker requires an actual valid, present, entirely zero 20-byte
FmpState with attributes3, INFO state flags0, running version `0x001a0009`,
floor `0x001a0009` and no attempted version. It performs distinct genuine CHECK
and SET transactions on the original signed bytes for attempt `0x001a000a`.
CHECK requires whole-medium and owner-state equality. SET requires exact full
COREBOOT route bytes, unchanged complementary non-SMMSTORE spans, and the actual
canonical successful owner state. CLOSE precedes real ResetSystem Cold.

The newly installed, independently compiled cold oracle requires INFO running
`0x001a000a`, durable history and last attempt `0x001a000a`, floor
`0x001a0009`, all four durable-state flags, successful status, and real
GetVariable attributes3/size20. It also performs two actual CLOSE RPCs and
post-close INFO. UART markers report successful exact assertions, not printed
numeric variable contents. The required cold-running/history marker is separate
from the earlier metadata-only markers.

Each restore starts from an owned byte-identical copy of that same reviewer's
successful updated pflash, not pristine newer firmware or zero seeded state.
The restore worker requires actual prior running/history/last attempt
`0x001a000a` and floor9, then CHECK/SET authenticates attempt `0x001a000b` to
restore exactly the original pristine COREBOOT content compiled as
`0x001a0009`. The restored cold oracle independently requires running9,
durable history and last attemptb, floor9 and success. This deliberately proves
original-content restoration with newer authenticated attempt history, not an
invented compiled running versionb or rollback-resistant hardware counter.

## Inputs and readback

The immutable fixture directory is
`/home/sean/q35-authenticated-capsules-after386.rDjaVw`. Pristine copies were
packaged with the actual normal cbfstool and the independently matching cold
oracle ELFs before signing, never patched after installation. Production capsule
generation and validation bind full ROM, GUID, full 32-bit attempt version,
floor, zero embedded drivers, FMAP and actual CMS. Only the COREBOOT route is
installed; SMMSTORE contains authorized canonical state writes, and SECURE_PROBE
and other unlisted spans remain unchanged.

| Immutable artifact | SHA256 |
| --- | --- |
| Original pristine image | `20582962fa53d2e99bb8a013d3eed49ead9ad559ed517a0ea4023e46010dd48a` |
| Newer pristine image | `0b7d3ab52883f5e225ae26f8694a55277883284d490ccc2645f61ae0a35396fe` |
| Newer full capsule | `ae8dd681502f750024ad3db51ee82be4039d6d32e514a3819273d89721bbc9ff` |
| Restore full capsule | `b7047ea0210d4859c54cc31c58203da752e427b325e16d27cf8297a7c276751c` |
| Newer authenticated suffix | `f338c1a28e6d4cf633ba56fdd556709d82b7e5cd64946fb406557567fd309362` |
| Restore authenticated suffix | `822bfec13c7d8ecc2605c2be4f9019583ee0497a2c477e84a1b62115f5c07a33` |
| Final updated medium, author and peer | `4cc01cf0e907bf9c37b5cf5189a2365ec0efab305f4a512fff92e4d8e6fadb27` |
| Final restored medium, author and peer | `9dffef14b6eb055476877e87319ae4fbffc33ed68ea2cfbe41cb348a93098954` |

HOST readback compares every installed COREBOOT byte against the owned
`target_image.bin` actually embedded in the worker, and compares SECURE_PROBE
before/after. Final whole-medium equality with the pristine ROM is intentionally
not claimed: legitimate SMMSTORE owner history differs. The copied before-input
manifests were checked after the runs, including the original successful medium
remaining unchanged when its copy was used for restore. External ROMs, ELFs,
signed capsule bodies and guest media are retained locally but only their hashes
and bounded public source/config/raw receipts are archived here. No private
signer or key is copied.

Earlier failed CHECK/SET timeouts and bounded GDB/syscall diagnostics remain
separate in `native-apply-diagnostics-after385` and the preserved local failed
outputs; they are not reclassified as successful runs by these later passes.
The corrupted signed-input denial packet remains separately qualified in
`native-check-denial-after384`. Production Core/SystemFmp RAM routing with the
QEMU FMP fixture disabled and genuinely coordinated DISK parity remain open.
