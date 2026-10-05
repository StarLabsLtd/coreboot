Legacy TCG boot-variable event-set HOST gate, 2026-10-05

Twenty-four original logs/config/command/dependency records plus this note.
Flat records map to /home/sean/tcg2-legacy-eventset-host.OmP0Fb; build/build-o0
preserve their original relative paths. Exact mechanical copies, no ELF/media.
Source signedcefb25c85fade3d244635ef67b06829d8541b3fd (PR696), parent signed
427cfffca4d3e5a14214e5519f42031f4da83326 (PR695), four-file binary delta
64e8e18477b789d1974f311f35aa72ed5c906a4a592483198ded6598e74f7393.

Actual pinned EDK2 aab7b589fc59b7e2b8fb7eb79519bf1a5e5a5272 source was read.
Production removes extra Current/Next/current-outside-order legacy events,
retaining BootOrder and its listed Boot#### entries in order, including repeats,
at PCR1. Generic digest/wire/bank/PCR behavior, refusal to iterate odd-size BootOrder,
measurement/read/allocation failure policy, boot selection and EFI variables
are unchanged. Changed PCR1 baselines require affected policies to reenroll.
Failure-policy parity, selected-Boot guest/live-PCR replay and full PFP
compliance remain separate; launched-image measurement does not substitute
for outside-order option metadata measurement.

Existing included-real-driver fixture proves names/GUIDs/data/order/PCRs,
no Current/Next reads at separator and repeat-callback boundaries, duplicates,
uppercase hexadecimal option names, missing/malformed/read/allocation/measure
failure paths, generic caller PCR5 and secure-variable PCR7, once-only attempts
and separators. Initial O0/O2 strict sanitizer invocations failed2 linking
unused callback boundaries retained by instrumentation; old-compile.log also
failed1. Four exact-signature unused-boundary wrappers unconditionally abort
on invocation; no fake success or sanitizer/global-instrumentation relaxation.
The Make link flags expose those existing model boundaries explicitly.

Fresh genuine standalone O0/O2 configurations agree. Both final public entry,
measure, service and event-log target groups pass: positive-o0-retry.log and
positive-o2-verified.log (isolated wrapper7b69db:0). Strict C11/Werror,
-fshort-wchar, O0/O2, ASan+UBSan/no-recovery/frame-pointer/no-PIE were selected;
both retained real-driver fixture binaries link both sanitizers. Native-stage
79276 reaped906b4a:0 produces real ELF64 EXEC14248bytes entry0x100000. Final
sanitizer group repeated after that stage changed shared command/artifact flags.
An intermediate post-stage command's trailing diagnostic queried nonexistent
cdk2-native.elf and returned2; that diagnostic is not a failed native-stage or
the final isolated successful target group.

Full signed427 old-driver source is byte-exact, compiled with the same final
fixture and all boundary wrap flags0; it aborts raw134 on the exact no
Current/Next-read assertion at separator. No ASan/UBSan finding. Original
old-compile refusal remains separately preserved; current old source/binaries
stay at original paths. The driver dependency file covers its one included
translation unit, not universal suite/compiler/environment closure.

The initial nonquiet multi-file style invocation returned wrapper2 even though
upstream reports zero diagnostics. Single-file driver/fixture style reports
zero; proper quiet/show-types multi-file invocation independently returns0
with empty owned-c-style-quiet.log. Do not relabel the earlier wrapper failure.
This proper invocation also confirms PR695's changed-C style independently
(display host owned-c-style-quiet.log remains at its original path).
No loaded firmware/TPM/PCR/hardware/all-profile or whole-project signoff claim.
