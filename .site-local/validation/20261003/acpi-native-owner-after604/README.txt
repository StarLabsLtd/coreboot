ACPI native ownership types and unused diagnostic bytes API

This finite packet preserves two separately reviewed source commits:
  type WIP: 7d39fb416b3a34b257e3abe124491608cf0b247d
  unused API removal: 33c586738eb34b08d05c24e991a276169dae5280
The baseline is signed 15c3bc1109ab39f06d1298895fdd95ae0dec6759.
Their exact six resulting bodies were restacked after603 as type57d3f44b3a
and deletiond9ea65578b, ready604. No scanner change is included here.

The type commit converts native ownership status/key/count/byte fields and
native allocator signatures. It distinguishes a native contextual allocator
from the genuine TPM MS allocator family. Actual EFI/MS callback signatures,
ACPI records and packed HOB wire fields remain unchanged. Storage flags stay
unsigned bytes, not bool. All claims are for the supported native x64 build.

The real MS install-table wrapper now uses a native uint64_t key local and
copies it to the genuine UINTN output only on non-error. NULL is forwarded to
the native guard; errors preserve the caller's high64 key. This is a deliberate
boundary adapter change, NOT a whole-object byte-parity claim for the driver.
The fixtures add actual NULL/no-allocation and error/key-preservation checks.
The baseline combined ACPI/TPM header compile failed on conflicting allocator
typedef names; the candidate combined/repeated header fixture passes.

Type proof: actual P0/P1 O0/O2/Os builds produce 30 baseline/candidate object
pairs. Twelve native owner/diagnostic whole pairs match. Six driver whole
pairs differ at the reviewed install_table adapter; every other disassembled
instruction/relocation section matches. Twelve enhanced test object pairs
differ as expected from added assertions/includes/type-string and line changes.
The derivative section comparison is not a whole ELF/data identity claim.

Six actual native initialized layout/padding/output comparisons match at
P0/P1 O0/O2/Os. These exercise a modeled fixed-address HOST allocator, actual
ACPI initialize/install/uninstall/destroy, immutable source tables, root
overlays, all256 native storage values, and high64 keys/statuses. Only process
code/context/record pointers are masked. Actual fixed modeled table addresses
remain in the output. The P1 diagnostic sink is modeled, not native CBMEM or
a firmware HOB producer. Four strict ASAN/leak/UBSAN no-recover comparisons
and eight real table/driver fixture executions pass.

Genuine owned P0/P1 configurations are resolved by normal Make. P0 DEBUG is
off, so diagnostic PE string assertions are conditional/skipped; P1 DEBUG and
DIAGNOSTIC are on and the actual diagnostic PE string gate executes. Both
profiles pass native-acpi-table-test, native-acpi-table-diagnostic-parity and
native-tpm2-acpi-table-test. Type timings are GNU3.59/4.08 seconds; deletion
timings are3.71/4.01. The exact joined603 final gates also pass at3.71/4.04.
Those are different enclosing runs, not a controlled performance comparison.

The deletion has no tracked call/address users: it removes only the diagnostic
bytes declaration, debug/release macros and function. Message/value printing
remains. The same six representations, four sanitizer comparisons, eight
fixtures and profile gates pass after deletion. P0 diagnostic whole objects
match. P1 comparison removes precisely the unused function section and its
now-unused cdk2_diag_bytes undefined symbol, normalizes both objects with
objcopy, then compares whole remaining objects at O0/O2/Os. This is explicit
unused-section subtraction, NOT original whole-object parity after deletion.

Retained failures: the original combined-header typedef collision; an initial
mechanical MAX_UINT32 token-substitution compile failure; the first deletion
comparison that removed only the function section and left its undefined
callee symbol. These are not candidate firmware/runtime failures or passes.

No hardware/QEMU boot, native MM authorization, whole-project lint success,
new board selection, scanner safety or final signoff is claimed. Binary
objects/executables, ROMs, guest disks, private keys and complete Git history
are deliberately excluded. Proof helper sources/scripts, public configuration
headers and exact raw receipts are retained; the original external artifacts
remain separately preserved.
