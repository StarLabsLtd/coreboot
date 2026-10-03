# PR624 retained-RAM disk staging — finite HOST receipts

This packet is HOST-only validation for ready [PR624](https://github.com/StarLabsLtd/cdk2/pull/624).
It contains source, configuration, logs and compiler dependency maps, not
executables, objects, firmware, disks, variable stores, capsule images or keys.
Root owns any evidence-Git publication. No native observer is implemented here.

## Source and lineage

The original signed checkpoint is `a9f4f6ea45cf13e4f54c8e8b7319b73a1ba3989f`,
parent619 `1baa1dbbf5050b62a9fa27c2b373af84df9b92ce`. Ready624 is signed
`3b07610cc5a03d3d910217fdada7f624f64effc2`, parent623
`cb71f948001ddfcc2b366d645b12a61e08d3ff1a`. The four test/recipe/linker blobs
are byte-identical between checkpoints; the entry delta has the same stable
patch ID `6c673f3c78477a8ff2e65870750b1dbce6a35390`. The joined whole entry
also contains the intervening reviewed LVGL changes; it is not claimed
byte-identical to the old whole entry. Both eight-member source archives include
the five owned files, the included existing handoff fixture, and the unchanged
real FAT binding/capsule-disk code.
Git signature transcripts were collected after signing, not before test launch.

The real normal generated header/config come from
`disk-request-normal-protected-core-after619.1Zg8yk`: capsule profile,
SystemFmp, linear boot and DMA handoff selected. The genuine P0 inputs come from
`linear-boot-api-gate.f3YlpW`: profile/SystemFmp/DMA disabled, linear boot on.
No forced RAM bit or storage grant is used. The existing handoff script's
pre-existing LINEAR_BOOT0-to-1 transform is a no-op for these already-enabled
normal inputs. Its temporary copied header is hashed by the nested stage test.

## Actual passing runs

| Receipt | Actual status / elapsed | Scope |
| --- | --- | --- |
| author-final /59939 | 0 /56.49s | final lifetime staging script on original619 |
| independent-final /84203 | 0 /56.72s | separate worker, original619, exact same stage bodies |
| supported-619 /74864 | 0 /126.90s | supported handoff recipe after strict sanitizer flag correction |
| joined-623 /62124 | 0 /129.26s | same supported recipe on exact ready624 tree |
| p0-619 /56611 | 0 /14.49s | genuine nonselected P0, O0/O2 unsupported path |
| p0-623 /77458 | 0 /15.47s | same P0 path on exact ready624 tree |

The supported handoff runs exercise the existing selected-ESP scanner and six
disk/support/request guard causes, then the new staging script. Only the three
request causes have full inverse comparisons in the existing handoff recipe;
the older disk-delivery/support/attributes causes do not. The new script
runs 30 scenarios plus four unsupported/cardinality cases, O0/O2 positives,
the actual Core scanner lifetime boundary, and three exact guard-discard causes.
The first modeled Stop error is covered, **not** errors at all four positions.

The three causes discard (1) the Core post-teardown completion guard,
(2) held storage identity equality, or (3) held private-control identity equality.
They must hit respectively `!filesystem_retired`,
`stop_calls == 1 && timer_calls == 0`, and `pci_calls == 0`, assertion134,
with no sanitizer diagnostic and byte-exact full inverse TU. These causes use
the SAME O2 heap/stack ASan (globals disabled) and UBSan as passing positives;
they are not uninstrumented. Raw logs and exact mutation/inverse source are kept.

Actual Core/scanner/allocator/image-transaction/SHA code is joined to modeled
resident publication and controllers. Query is the real generic
`cdk2_capsule_query` with `support=NULL`, **not** Runtime FMP structural support
parsing, CMS/MM verification or native controller execution. The linker RWX
segment and ASan globals-off are explicit HOST-only fixture qualifications.
TPL restoration, callback/owner/generation/transaction drift and returning
UpdateCapsule failure are modeled assertions, not DMA-drain measurements.

Each compiler map is the union of actual O0/O2 `-M` results under the same
compile vector, with original source/system headers, copied configuration and
cc/ld/sh/awk hashes. Raw maps retain their original absolute paths; this packet
does not mirror every compiler header or copy tool binaries. The independent
final map has 189 rows. End-of-run checks are in raw logs. The independent
whole five-file envelope includes old handoff SHA `d80bf46c...`, predating the
later sole `-fno-sanitize-recover=all` flag. Four other authored bodies are
unchanged; final supported/joined runs own the corrected recipe coverage.
No historical manifest is claimed to match a subsequently edited file.

## Concrete filesystem lifetime

Real `src/modules/fat/binding.c`, Stop line235, releases `mount->simple_fs`
after unpublish. Real `src/lib/capsule_disk.c` completion line1323 dereferences
`filesystem->open_volume` before checking processing status at1333. Thus even
failed processing must not enter completion with the retired filesystem pointer.
The fixture models actual registry uninstall at the first FAT Stop and marks
its retained counter object retired. A GNU wrapper asserts that boundary before
delegating the real completion function. It is not an actual UAF test, a native
FAT free, an I/O reopen claim, or a blanket prohibition on pool cleanup. The
passing real scanner test separately observes one discovery open and zero deletes.

## Preserved failures and limits

- The evolving fixture has an initial wrong scanner-status assertion134 and an
  actual LeakSanitizer failure (4180 bytes/three allocations); later evolving
  scanner and expanded positives are separate. These logs are not final proofs.
  The initial ABI compilation error was tool-visible only: no original raw
  receipt is invented here.
- author15490 and independent33914 exit2 (16.51/16.56s): positives pass, inverse
  awk multiline ternary fails; no causes run. The final portable if/else fixes it.
- author34814 and independent27375 exit1 (22.18/22.40s): positives pass, first
  uninstrumented mutant link lacks boot-logo/native-x86 symbols; no cause runs.
  Final causes retain successful positive sanitizer flags instead of adding TUs.
- author94797 and independent93229 exit1 (28.93/29.43s): old latch mutant
  survives with exit0. It does NOT prove a completion reopen or guard defect.
  The revised cause is grounded in the real retired-interface preflight above;
  no artificial no-FreePool contract was added.
- supported67335 exit1 (12.27s): GCC15 O2 ASan SHA256 array-bounds warning is
  promoted to error before fixture execution. The sole stronger sanitizer flag
  fixes the recipe compilation, not vendor code; no warning suppression is used.
- Initial PR metadata command53338 exit1 guessed a nonexistent base branch;
  actual623 lookup corrected it and command3533 created ready624. This was a
  metadata/tool failure only; no failed PR/ref mutation or native test occurred.

The source file is retained. Complete request consumption precedes Query; a
later preflight failure does not automatically restore OsIndications. This is
bounded linear timing, not identical EDK2 CoD semantics. No late MM grant or
closed-RAM reopen is introduced. The future four-epoch9/9/9/A three-reset native
sequence, real controller drain, hardware and power-loss gates remain open.

## Replay

`python3 check-receipts.py` checks packet checksums, exact source/inverse equality,
passing scopes, failures, configuration and absence of binary payloads. It is
read-only, portable and does not rerun compilation, firmware or QEMU. Optional
`--originals` compares copied raw files to their surviving host originals;
`--git /path/to/cdk2` verifies archive members against both exact Git commits.
An original temporary header in a raw map may have been removed by its original
handoff script after a successful end check; no reconstructed pre-launch map
or new after-failure success is substituted.
