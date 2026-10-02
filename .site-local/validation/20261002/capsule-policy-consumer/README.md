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

## Signed projection and wider source-contract replay

Reviewed twenty-path consumer checkpoint is signed
`f0a4d91308d47586288c5be353b6b3f5a0b99519`. Root's signed linear copy
`366494bc8871a13f1dedd840b5a8ec58ecf75a48` follows the reviewed hash assertion
cleanup. Its independent patch-equivalence check passes, and root's combined
six-target replay session 51863 actually reaped exit 0 in 30.945s: coreboot
HOB, capsule runtime/disk, FMP transport, compiled DXE disk handoff and linear
source-order checks. The exact raw output is included. Ready PR498 publishes
this linear checkpoint; this is not native transport or hardware sign-off.

Actual reaped wider gates:

- Profile configuration/native composition: session 22418, exit 0, 56.624s.
- Capsule disk native-artifact contract: session 97168, exit 0, 4.788s;
  replacing an obsolete no-op fixture sed with a copy: session 73274, exit 0,
  4.898s. The fixture explicitly selects the real coreboot capsule profile.
- Actual owned-policy linear source-order check: session 21837, exit 0.
- Actual DXE disk handoff fixture: session 76262, exit 0, 9.280s.
- Strict O0/O2 DXE disk positive and exact-source guard-discard mutation:
  aggregate session 66322, exit 0, 24.280s. The deliberate permission-discard
  binary returns 1 with the required filesystem-side-effect failure and no
  sanitizer diagnostic; it is not a SIGABRT-134 mutation.

The DXE fixture exercises disabled/RAM/disk/both transport masks from an owned
HOB, malformed-policy denial, S4 suppression, and disk processing. No pending
RAM capsule is present in this fixture, so it does not prove that delivery.
Old tests demanding compile-time disk policy are replaced by the real table
contract and causal removals. Native PE replaces a nonexistent retired FFS
target; the retained-FV import-denial test remains.

Additional preserved failures include inherited READY configuration skipping,
the old scanner expectation, the obsolete FFS target, missing pinned BearSSL,
the unenabled old artifact profile and the old DXE fixture's zero processor
policy. A separate missing LVGL invocation is also retained. Only the selected
BearSSL submodule was initialized, at the exact gitlink
`8ef7680081c61b486622f2d983c0d3d21e83caad`; the shared LVGL input was read-only.
