# Protected boot-entry HOB fixture gate

This unexecuted recipe runs the unchanged `protected_variable_boot_gate_test.sh`
against the isolated signed ready658 source plus the reviewed one-file fixture
diff. It reuses the actual genuine standalone DEFAULT656 resolved config/header,
not a new Kconfig admission or fresh Core/ROM. The original failed DEFAULT receipt
is preserved. The retained shared Make source list is exactly the entry HOST list
for those configuration values; the lifecycle-composition list is empty and the
software-hash append is not selected.

The shell runs the legacy, supported and missing-security local profiles at O0
and O2 with its unchanged ASan/UBSan flags, all original entry/dispatch/close/HOB
immutability assertions, and the scoped missing-security O2 guard-deletion mutant.
The new canonical validator assertions exercise corrected owned geometry and the
old stale geometry. These are HOST fixture checks, not execution of firmware
drivers or a guest boot.

The recipe records the exact command, raw shell status/time, tracked source bytes,
signed base, reviewed diff, config/header bytes and enumerated selected compiler,
support and shell tools before/after. Any nonzero raw status or closure mismatch
keeps aggregate failure. No Core/producer build or VM is requested. Execution
requires root recipe release and an assigned free compiler lane.
