Private diagnostic formatter native types

Candidate: /home/sean/Documents/.cdk2-worktrees/diagnostic-private-formatters-after584
Baseline: signed 0de72a0eff2f263056839eb206641f59757b7a8d (ready 584).
Only diagnostic.c and diagnostic_event.c differ. Shared helper bodies,
ordering and overflow expressions are unchanged; native char/size_t/uint64_t
replace private text, length and value aliases. No BOOLEAN storage, wire
record, owner atomics or EFI status changes.

Actual 63453 reaped 0, parity.log/time: 102 whole consuming-object pairs
(six native and eleven HOST, P0/P1 O0/O2/Os); six complete emitted binary and
layout pairs. Actual console/TIME emission and existing HOST-built HOB parser
outputs are bound; no native HOB producer or native-MM claim. All nine API
variants and previous high-bit/saturation/NULL assertions are retained.
The copied pci-fixture-parity.sh is NOT executed or claimed for this batch.

Actual 48718 reaped 0, boundary.log/time: eight direct production-TU helper
executables at P0/P1 O0/O2, baseline and candidate, strict ASAN/leaks+UBSAN.
Tests cover NULL/empty text, saturating size addition, decimal 0/9/10/MAX,
zero/max clock and existing guarded fractional-product overflow semantics.
No private helper was copied or replaced; the test includes actual TU bytes.

Actual 89991 reaped 0, fresh normal selected targets:
/home/sean/diagnostic-private-formatters-gate.MqmT1a/named.log and named.time.
native-cbmem-console-test and native-smmstore-test, own genuine resolved config
and read-only pinned real dependencies/coreboot inputs. No READY/header seed.

Both candidate source-before hashes remain unchanged. Default project patch
checker: 0 errors, 0 warnings, 181 lines; existing ignored-message policy
unchanged. This is not raw whole-source lint or full firmware acceptance.

No new setup failures occurred in this formatter proof. Prior phase-batch
harness failures belong to that earlier packet, not these successful runs.
