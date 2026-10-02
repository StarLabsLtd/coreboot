# Capsule-delivery consumer checkpoint

Consumer base: CDK2 `6ccd3f13f4099477c2b26d953e38bbc50da82192`.
Producer wire definition: reviewed coreboot `94eb7d0cbb` (PR358).
The consumer source remains a reviewed work-in-progress, not a published final
native capsule activation or hardware round-trip result.

Root four-target replay session 72251 actually reaped exit 0: coreboot HOB
construction, capsule runtime, capsule disk and SystemFmp transport tests.
Unit runtime 13.876s, CPU 22.963s, peak memory 113.7MiB. The owned HOB retains
the validated source snapshot; missing or malformed policy disables delivery
without preventing ordinary boot. The single-call append helper was inlined.

Checkpoint source SHA-256:

- coreboot_hobs.c: `4f6449e5d8a261d882378e8be4aa3f7127e5923c5458555c377fe70ea1776741`
- capsule_runtime/entry.c: `811ae5cbef991b2239212375851403042314f9d4aae141d8fb4d7c5f4f6dbae3`
- capsule_runtime_entry_test.c: `13b45e2131d485723b061ee32685476002915339b8cb288f5b076e32c002e6b9`
- build/capsule-policy/.config: `0807ea9108e13b448b440fd2049753b4e27710488d393257868d123fdb5a5b48`

Session 9656 actually reaped exit 0 for freshly compiled real capsule-entry
recipes at O0/O2 with strict ASAN/UBSAN. Its receipt is included separately.
Disabled RAM delivery returns UNSUPPORTED before zero-limit capacity handling;
the fixture exercises actual EBS notification and checks no update side effects.
Disk-only boot processing remains a separate positive, not RAM-update permission.

Earlier failures are preserved: the first real-entry matrix found disabled
RAM updates returning OUT_OF_RESOURCES; the first root rerun subsequently
found a test-fixture event-slot overflow. The diagnostic attempt missing stdio
and the later failing retry-status output are also failures, not passes.
The fixture now resets event state for each scenario and bounds event slots.

Outer capsule limits do not substitute for the separate authenticated FMP-payload
staging capacity or writer admission. Positive reset-persistent RAM publication,
native transport execution, capsule round trips, residual non-linear BDS limit
retirement and hardware validation remain open.
