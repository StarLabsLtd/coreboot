Unexecuted bounded HOST gate for exact two-file144d259c legacy BOOT digest fix
on signed6616faf. Root and independent source/outer review are required first.
Ten existing C HOST models use the source inputs and model-only defines from
src/boot/Makefile: transport, commands, event log, measurement, service, entry,
diagnostic coalescing, platform HOB, software hash and TPM2 ACPI table. Each runs
at strict O0/O2 and separately ASAN/UBSAN. Software hash provenance/header tests
are unchanged. The PE-file diagnostic parity utility is not run or claimed.
Platform-HOB ASAN retains heap/stack instrumentation but disables global
registration, as existing full-entry HOST fixtures do, to keep unrelated driver
callback sections unrooted through GC. All other models keep ordinary ASAN.
An immutable genuine661 TPM build header/resolved/input/producer config supplies
the configuration; no edited or manufactured enablement header is used.
Actual compiler -M dependencies are hashed before models; tracked source,
vendored hash implementations, compiler tools/support, config/recipe/old source,
head/status/diff and resolved tool paths are checked before and after. This is
finite enumerated HOST evidence, not complete dynamic dependency attestation.
The signed old661 measurement source uses identical final tests/real hash deps
at O0/O2 and must return1 with the specific variable KAT refusal. Raw statuses,
commands and timings are preserved separately from opposition qualification.
There is no firmware/producer build, VM, canonical write or prior receipt edit.

KAT inputs are fixed data01020304 and explicit little-endian 40-byte payload
01000000020003000405060708090a0b02000000000000000400000000000000
6400620001020304. Eight literals were generated with independent Python hashlib
during source preparation, not calculated from the candidate at test runtime.
The tests exercise real SHA1/256/384/512, GUID/name/data mutations, full logged
record bytes and unchanged BOOT PCR5/DRIVER_CONFIG and AUTHORITY PCR7 arguments.

Separate open parity decision: current selected-BOOT production chooses PCR5,
whereas pinned StarLabs EDK2 release26.09 aab7b589 chooses PCR1 and the current
TCG PFP1.06 describes PCR1/BOOT2. This patch changes only legacy BOOT hash input,
not PCR placement, event choice, wire layouts, algorithms/banks or full log data.
It is not evidence of full PFP compliance or runtime-selected Boot#### coverage.
References: pinned SecurityPkg/Tcg/Tcg2Dxe/Tcg2Dxe.c MeasureVariable/ReadAndMeasureBootVariable,
TCG PC Client PFP1.06r52 legacy BOOT and BOOT2 sections, and systemd v257
src/pcrlock/pcrlock.c validate_variable(). Strict guest parsing remains unchanged;
the functional guest currently produced zero full HEX/PCR45 blocks, so an OS
fixture rebuild is separate unfinished work and no successful TPM oracle claim.
