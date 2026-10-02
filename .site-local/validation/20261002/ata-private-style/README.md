# ATA private-model style checkpoint

Root's signed linear copy is `be113da9b9ef890f3249b3b80539dfa960544f0f`
(ready PR502, parent `957d1f2d7d2cf1cfc02909d21040ff9e0fc2ecdf`).
Its complete one-path patch body and source bytes match the author checkpoint;
the source path is also retained in `signed-linear-source.tar`, not a full
build closure. Root actually repeated all five module gates at this linear
head, session 56211 reaped 0 (6.406s elapsed, 10.695s CPU, peak 70.7M), retained
in `root-linear-be113.log`. A peer verified every copied author packet file
byte-for-byte and accepted the stated qualifications.

Signed source: `fa8b5baf89c7a75e7ccdd4a3019846fe40541637`, parent
`a5db484eabb11486cc3bd48b1240a3c2e8e03c31`, signature G. Only
`src/modules/ata_bus/model.c` changes; SHA256
`074b2e9f860bba71d41e27d5e06022f2663535d519f0de710c477eae4c875788`.
Root and the independent BDS author accepted the bounded source review.

Private fixed-width/index/bool types replace equivalent aliases on the supported
x86_64 target. Public UEFI/protocol signatures, callback calling conventions,
wire structures and constants stay unchanged. The sole-use get64 helper is
expanded as the same two bounded little-endian get32 reads and 32-bit shift.
No vendor or header source changes. Actual coreboot clang-format 20.1.8 was
used, followed by manual readable ternary/initializer layout corrections.
Coreboot profile SHA256:
`c191fe47d2b59fdceeed23c6e737a4f73b438fe7994559cdea815ddea6ded319`.

Actual receipts:

- Five focused model/I/O/block/binding/entry Make tests reaped 0 (session18255).
- Real native AtaBusDxe.efi compile/link/PE validation reaped 0 (session89310).
- Six freshly compiled strict ASAN/UBSAN model binaries, default/selected header
  sets times O0/O2/Os, reaped 0 (session83656).
- Three freshly compiled differential baseline/candidate parser binaries each
  compared 100000 directed/random IDENTIFY operands, exact status and complete
  media output, under strict sanitizers; aggregate 0 (session71889).
- Focused actual source checkpatch reaped 0, silent (session76427).
- Durable replay of those retained binaries, five module tests, native PE target,
  checkpatch, diff-check and source/config/image hashes reaped 0 (session44998).
  Its unedited actual output is `replay.log`; `replay.sh` is the exact replay.

Full native production-flag objects at O0/O2/Os differ at every optimization:
there is **no whole-object byte-parity claim**. Removing a real helper/private
bool representation changes code generation; at Os parse_identify is 11 bytes
smaller, and the other exported functions have identical symbol sizes. Original
and candidate objects are retained. This is bounded source and modeled/native
build validation, not whole-boot, platform portability, hardware or whole-tree
style sign-off.

The initial relative Make PE path failed 2 (no such target); the actual absolute
native target subsequently passed. An initial attempt to invoke checkpatch-paths
as a source checker failed 2 because that inventory helper requires a ledger;
the actual cdk2-checkpatch source invocation subsequently passed. These failed
invocations are qualified here and must not be relabeled successful tests.

Config header hashes in the actual replay are retained in its raw output. The
selected set came from the readonly canonical checkout at test time. The model
does not itself add or switch configuration choices. This directory is a
bounded proof packet, not a complete compiler-input or reproducible-build archive.
