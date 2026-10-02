Diagnostic API/formatter and unused configuration receipts after PR589

Finite historical source/build/HOST proof, not a new firmware or release gate.
CDK2 source checkpoints:
  584 0de72a0eff2f263056839eb206641f59757b7a8d, baseline 8ce4f648412fd166cc3875968db835a32118c8fb
  586 380e197c1690e1c987e38569c6292db12a790c27, body baseline 0de72a0eff2f263056839eb206641f59757b7a8d
  589 c05b7c8d174181b367764c344377d26edd228d54, baseline b6f37a6f095c5e541644cda3412b590d1cce04ce

All three final source checkpoints are signed. Sources contains only signed
changed-file baseline/candidate snapshots, not whole Git history or a full
firmware source export. The actual external proof scripts retain their original
absolute paths; they can be replayed with a new output root and signed full
source exports from these commits. Snapshot archives suffice for exact changed
body/hash review, not by themselves for compiling every transitive consumer.

Each numbered proof/ directory copies the original retained scripts, helper C,
source-before hashes, raw logs/times, serialized output bytes and layout text.
All copied proof files were compared byte-for-byte with their originals.
These scripts are final proof bodies, not reconstructed pre-failure variants.
The bin files contain diagnostic/model representation output, not executables,
firmware media or private cryptographic data. Compiler objects and executables
are intentionally excluded; object comparison receipts refer to actual retained
external objects, not objects included in this packet.

584: actual 53665=0, 102 whole object pairs (six native + eleven HOST consumers,
P0/P1 O0/O2/Os), six full output/layout pairs. A separate actual 41632=0 compares
six PCI fixture objects with the genuine isolated diagnostic=0/1 header shapes,
NOT P0/P1. Actual normal selected 3544=0 covers seven named consumer targets;
39209=0 covers genuine diagnostic_test.c O0/O2 strict ASAN/leaks and UBSAN.
The wire timestamp saturation and optional BOOLEAN predicate are unchanged.
Produced HOB input is the existing HOST-constructed fixture parsed by production
code, not an actual native HOB producer. The two prior harness failures are
retained: a full P0 header conflicted with the isolated PCI fixture, and a proof
assertion initially compared unsigned input against the signed saturated wire
timestamp. No production guard or arithmetic was weakened to repair them.

586: actual 63453=0, 102 whole object and six full output/layout pairs; 48718=0
exercises eight actual production-TU helper binaries at P0/P1 O0/O2 with strict
SAN and boundary cases. Actual 89991=0 runs normal CBMEM/SMMSTORE consumers.
The copied PCI parity script was NOT executed or claimed for this leaf.
The signed final parent includes docs585; its two code bodies equal the tested
draft on584. There is no generic full bool/GUID/wire conversion in this slice.

589: genuine resolve-test.sh outer0 compares actual baseline/candidate default
and protected resolved config/header bytes. After removing only the three
retired-symbol lines, every remaining byte matches; unfiltered config hashes
legitimately change. Actual corrected13324=0 compares 48 whole native objects
(eight boot/Core/CPU/APIC/PE TUs, respective generated P0/P1 headers, O0/O2/Os).
Actual P0 stage/coreboot/inventory46286=0 and P1 same10040=0; existing profile
and linear31328=0; Kconfig/patch lint0. Both preceding profile aggregates failed
at missing BearSSL in the new WT, despite passing linear tests. An environment
override did not replace the existing Make-owned := path; the successful run
initialized the genuine clean8ef pinned submodule. The first object script
guessed lib/pe.c rather than actual boot/pe.c and failed before completion.
Those dependency/path errors are harness failures, not firmware failures.

Actual generated/copy profile headers are retained. The 584/586 proof profile
headers are copied genuine existing P0/P1 inputs, not newly resolved by those
proof scripts. Their named gates retain their own actual resolved config/header.
589 retains both actual baseline/candidate configs/headers and the explicit
three-symbol-filtered comparison inputs. Full numeric timing and compiler
diagnostics remain in original raw .time/.log files; named log files contain
recipe output rather than a reconstructed shell invocation. Each proof's
original README gives its exact existing target list and narrower scope.

No native QEMU/MM, hardware, interrupted-write, final whole-suite or whole-lint
closure is inferred. Default patch lint passes are distinct from historical raw
checker failures. Later retirement of diagnostic_event.c does not retroactively
turn its explicitly compiled proof object into a production build consumer.
No keys, disks, ROMs, ELF/executables, object files or duplicate Git history are
included. Root alone owns evidence publication after independent packet review.
