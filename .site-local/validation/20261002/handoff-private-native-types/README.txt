SystemFmp handoff reader: native private types, bounded WIP proof

CDK2 signed WIP 3fc0aba2587ee817fdaf718bc1e5c5aa8b79763a, parent
66966831811127781302929487f01efaed2dbec4. One production C file changes:
private predicates use bool, unpacked offsets use uint64_t, byte accumulators
use uint8_t, bounded counters use uint32_t and lengths use size_t. Arithmetic,
validation order, public EFI_STATUS entry signature and header models are
unchanged. The exported cdk2 handoff model is not itself a coreboot wire record;
its existing header aliases are left for a separate classified conversion.

Root normal ordinary/O2/ASan+UBSan gate returned 0 before the final blank line
between owned and standard includes (GNU elapsed 0.60 seconds). Independent
worker replay also returned 0 at that same body with strict leak/UBSan settings.
The reviewer accepted the sole subsequent formatting line; root then reran the
complete normal handoff script successfully on the final signed body. The script
uses modeled record lookup and the actual production importer, not native MM.

Final default project checkpatch returned 0, 0 errors, 0 warnings, 161 lines.
The separately retained pre-format --strict invocation returned 0 but reported
22 CHECK diagnostics preferring Linux kernel aliases over stdint; those checks
are not hidden or presented as a completely clean strict report.

signed-source.tar contains every signed include plus the actual importer, boot
header and complete ordinary/O2/sanitizer test source/script. Extract and run
sh tests/system_fmp_handoff_test.sh for reconstruction. No generated authority
header, native firmware, capsule, guest media, private key or secret is included.
These are source-bound HOST WIP proofs, not final-parent or hardware sign-off.
