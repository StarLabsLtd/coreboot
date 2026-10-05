Framebuffer/firmware singleton consumer HOST gates, 2026-10-05

Twenty-five original logs/config/command-input records plus this note.
Flat logs map literally to /home/sean/display-firmware-authority-host.dbUpVQ;
build and build-o0 records preserve their original relative paths. All are
mechanical byte copies. Original artifacts and additional files remain there.

Reviewed four-path source delta against signedc1ea34fa972759dc1dbd4684cfc439cd3e1462b8:
fae1295ceab43c8e93ac616255208cc0d8e176f068cca06266e459a91e1a6991.
Only production changes are three existing first-match lookups changed to
existing unique lookup: graphics-HOB, firmware-info-HOB and early splash.
Optional absence, legacy37-byte framebuffer acceptance, geometry/resource
checks, and distinct splash PRH versus later graphics PCI policies remain.
Backend-test-only wrappers call the actual aggregate and splash consumers.
Eighteen structurally parsed cases cover absent/current/legacy singleton facts,
distinct duplicate facts in both orders, identical duplicates, short first,
short second and single short facts. On failure, the offending HOB is absent;
the aggregate may already have appended earlier HOBs and set the output pointer.
There is no whole-buffer transaction or successful splash drawing claim.

Both fresh genuine standalone defconfig profiles configured successfully.
Original positive-o0/o2.log compile-link failures2 exposed the newly reachable
real authvar ABI validator missing from HOST wiring. Adding its actual existing
implementation to the Make target then exposed the same dependency omission
in the separate deadline mutation compile; positive-o0/o2-retry.log failures2
remain. Both source lists now include the real implementation, no stubs.
Initial owned-c-style.log reports one new ternary colon parse/style error;
the expected-status local rewrite retains identical fixture semantics.

Final positive-o0-style-final.log and positive-o2-style-final.log public
native-coreboot-test invocations pass the deadline opposing mutant, interrupt
ownership check and actual model. Compiler flags used strict C11 warnings,
-fshort-wchar, O0/O2, ASan+UBSan/no-recovery/frame-pointer/no-PIE. Both retained
final executables link both sanitizers. The real freestanding
native-coreboot-stage gate separately passes; its earlier default build changed
the shared O2 artifact, so final sanitizer invocation was repeated afterwards.
Final two full changed-C checkpatch files and allowlist check pass. This is not
whole-project style completion or universal compiler/header/environment closure.

Old-lookups opposing intervention compiled0 using the final actual fixture,
actual ABI implementation and retained backend wrappers, reverting ONLY the
three production lookups. It returns raw1 with exactly20 fixture messages:
four early-splash policy failures, eight aggregate status failures, eight
offending-HOB publication failures. No ASan/UBSan diagnostics. It is not an
entire old signedc1 translation unit, which lacks the new test-only wrappers.
The copied intervention source and both executables remain at original paths.

No QEMU, hardware, successful splash-draw, capsule policy, or full-project
signoff is asserted by this HOST packet. The separately archived PR694 guest
exercised the parent, not this new four-path change.
