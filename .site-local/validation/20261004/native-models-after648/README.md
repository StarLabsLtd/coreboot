# Private XHCI/runtime changes and typedef lint recovery

Finite receipts for ready CDK2 PR646, PR647 and PR648. These are host tests,
source/config/tool checks and production-object comparisons, not a fresh
joined-head Core, QEMU guest or hardware signoff.

XHCI author `9033db5d2ca3` preserves installed protocol/callback definitions.
The existing four sanitizer executables pass in default, O0 and O2 recipes;
diagnostic parity and the final default aggregate replay pass. Independent
root replay runs all twelve retained executables and compares whole native
model/controller/diagnostic objects, with aggregate zero. PCI-adapter,
USB2-ABI and entry objects differ: typed EFI output temporaries and the private
USB2 state field change are not advertised as whole-object equivalence.
The private USB2 container shrinks from 539528 to 539520 bytes; installed USB2
protocol size/alignment remains 112/8. Strict warning suites fail with status
2 on both baseline and candidate at existing narrowing diagnostics. Initial
baseline object dispatch failures and the corrected commands remain recorded.
The broad protocol-region text comparison fails; the precise installed USB2
header comparison passes. The original author header checkpatch failure is
not reclassified as a pass.

Runtime author `0e18b7863ee9` passes eight existing model/entry sanitizer tests:
O0/O2 under both genuinely resolved default and strict configurations. Whole
production entry objects match the baseline byte-for-byte in both profiles.
The complete GCC -M input closures, before/after checks and independent root
eight-executable/two-object replay are retained. The earlier compiler-subtool
preflight and missing Kconfig PATH failures remain separate failed groups.
No new standalone HIGHLOW/list/malformed-relocation cases are claimed.

PR648 adds UINT8/UINT16 to the existing coreboot-checkpatch typedef input and
its valid/invalid-spacing tests, without suppressing warnings. The full
existing filter tests and actual changed XHCI header both pass; source
before/after checks and aggregate status are zero. This is a later scanner
fix, not a rewrite of the original XHCI lint result.

The collector preserves selected text receipts, scripts, dependency lists,
configs and source patches. It omits executable/object/archive binaries,
imported source copies, generated split-config trees and firmware/media.
`ORIGINAL_FILES.sha256` identifies original paths; every selected copy was
compared byte-for-byte. This is not a self-contained executable replay.
The source commits and linear ready PRs are published separately in CDK2.
No 32-bit portability, whole-project strict-lint, full feature parity or
release completion is inferred from these bounded results.
