Fresh PR694 Linux-origin firmware-setup/reset gate, 2026-10-05

Original records map to /home/sean/current693-fwui-qemu.PMINOC plus their
stored relative path. Plain files are mechanical byte copies; .gz files
decompress to the original path with final .gz removed (gzip -n transport).
Captured PPMs are actual QEMU framebuffer captures, not generated images.
No ELF, ROM, disk, variable-store binary, UKI or private payload is uploaded.
This finite packet preserves records, not every built binary or every file.

Firmware and observer: signed c1ea34fa972759dc1dbd4684cfc439cd3e1462b8, PR694.
Producer: signed 7ee34bed989c46913c3ee6672fb25e83227c3b6c.
Existing build-normal-fwui-fresh.sh and run-linux-boot-to-fw-ui.py were used,
unchanged. Initial build694 failed at the real local LVGL provenance guard:
the dedicated firmware worktree's LVGL submodule was uninitialized. This
failure is retained, not relabelled as a firmware pass. Normal git submodule
initialization restored exact 85aa60d18b3d5e5588d7b247abf90198f07c8a63 using
the canonical LVGL object-store reference; both borrower and owner remain.
Fresh build694-retry completed (process11870, reaped774ce2:0): payload
117.23 seconds/0, producer47.67 seconds/0. Selected source/tool/config and
loaded-Core ledgers remain bounded receipts, not universal environment closure.

Current requester UKI f8ee2b154e03fd36cbdec7c85731a7816cd08e68de19b42e3433e5bc5ca2a88f
and manifest e7c935e9a4b6ce3e986db17fae75898a8ade6960066516a6100c871961cc10d8
bind the corrected helper from signed009071b67f87378197a2729897d97e86e47ecaa3.
Requester HOST positive/reproducibility and old-requester refusal are archived
separately in linux-requester-refresh. No pin bypass or host variable seeding.

Existing run-deadline420/kill-after3 wrapper aef642, process56575 reaped0.
Top result failure null; nested same-vm outer/observer/QEMU statuses all0,
failure null, elapsed151.039744376 seconds within unchanged180-second budget.
This is whole-workflow elapsed time, not hardware boot time. Real Linux
OsIndications support/request/reset markers precede a QMP-observed guest RESET;
the observer then captures setup, navigates down, discards via Esc, observes
logo restoration/BGRT success, and boots Linux again. The guest verifies prior
variable update persistence, clears immutable before delete, verifies absence,
emits CDK2_ACCEPTANCE_OK and enters real ACPI S5 shutdown. The final disk equals
the admitted installed requester baseline; firmware changes are confined to
the actual protected-store FMAP region. Actual-media codec O0/O2 checks and
inverse comparison pass; the deliberate broken comparison mutant aborts on
its expected assertion. Its abort diagnostic is a negative-control result,
not a sanitizer finding or an unreported positive run failure.

Root separately viewed actual setup-form-initial and firmware-restored captures
after lossless PNG format conversion: focused native-scale graphical setup
with disabled layout-dependent controls, then restored centered STARLABS bitmap.
This validates this one Linux-origin reset/setup/return route. No hardware,
TPM/PCR matrix, all UI pages/mouse/accessibility, all EFI loaders, capsule
roundtrip/power-loss matrix, DMA-platform release or full-project signoff claim.
Original artifacts and additional records remain at their original paths.
