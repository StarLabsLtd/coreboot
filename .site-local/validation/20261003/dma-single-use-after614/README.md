# Two single-use DMA expression wrappers

This finite HOST packet covers ready CDK2 PR 614, signed source
`93e39d5a98547a7cd7028fdc7b7f38e3a2d855a7`, against its signed parent
`f7a44bcba6d32ba8003432542cab37f185e91085`. The production change removes
only `handoff_size` and `ranges_overlap`, substituting their exact expressions
after the existing count and range checks. It changes no public or wire ABI.

Root and a nonauthor reviewer independently ran the actual DMA fixture at
P0/P1, O0/O2 with ASAN leak checking and UBSAN. The existing DMA test script
also passed its ordinary, optimized, sanitizer, PCI arena, generation refusal
and 104-byte fixture checks. The narrow patch checkpatch result is zero
errors and warnings, not a whole-repository lint claim.

The immutable-baseline root replay completed with exit 0 in 2.62 seconds.
Os whole objects are identical for both profiles. O2 whole objects are not
identical: independent disassembly review found only the symmetric comparison
operand reversal at offset 0x5e3 immediately before JNE. O0 whole objects
also differ; removing local functions and relayout is not byte equivalence.
Raw disassemblies are preserved unchanged. There is no hardware, QEMU,
whole-project, DMA-controller enforcement or performance sign-off here.

Preserved failures are not passes: the first root gate stopped on O2 whole
object inequality; a later immutable-baseline replay completed its tests but
failed because its new output directory lacked the input manifest. The final
replay copied that exact manifest first and passed. Production source remained
unchanged during these checks.

`root/run.sh` records the compilation and execution recipe. Its three
arguments are output directory, candidate source root and baseline source root.
The captured configuration include paths identify the original generated
P0/P1 headers; their public copies are under `config/`. Extract the two signed
source archives for an independent replay and point those include paths at
the copied headers. Run from the candidate source root so the input manifest's
relative paths resolve. Archive coverage is finite and does not include full
build trees. `peer/replay.sh` records the independent sanitizer recipe.

No executables, object files, private keys, tokens, flash images, disk images
or variable-store dumps are distributed in this packet.
