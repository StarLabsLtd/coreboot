Normal623 phase-owned LVGL UI checkpoint — 2026-10-03

Consumer source: signed clean cb71f948001ddfcc2b366d645b12a61e08d3ff1a
(ready623). Producer389: 7ee34bed989c46913c3ee6672fb25e83227c3b6c.
Both normal profiles use protected variables/SystemFmp/LVGL/setup hotkey=1,
QEMU test FMP/QEMU acceptance/build debug=0. No firmware source was edited.
The interactive-test profile is NOT the production default: real Kconfig
resolution changed only CONFIG_CDK2_BOOT_TIMEOUT=0 to 2. The default remains0.

Preserved default-zero attempt, /home/sean/native-lvgl-normal-after623.zLwvcS:
  Core56248: actual0, GNU78.24/user98.08/sys23.66.
  Producer76594: actual0, GNU47.16/user84.48/sys11.34.
  Reference32300: actual0, GNU21.68/user32.47/sys6.15.
  VM14835: actual1, GNU39.71/user53.57/sys10.61; controller1/QEMU0,
    elapsed39.248945s of unchanged180s budget. Actual INPUT_UI completed
    SUCCESS0 but SetupHotkey returned NOT_READY with the zero-second window.
    The controller's physical-read connection error after guest exit is
    preserved. No successful F2/setup, timeout/hang or firmware regression
    claim follows from this profile. Screens show readable Preparing to boot
    and Starting operating system, not persistent Selecting.
    Default-profile hotkey usability remains OPEN: Kconfig default timeout0
    causes wait_for_setup_polled to return before ReadKey. The explicit2s
    checkpoint does not close production-default F2 parity. Default timeout
    policy, coreboot-symbol reuse and hotkey usability need a later reviewed
    decision; this lane did not change any production default.
  Earlier reference attempt: outerPython1/innerMake2, GNU0.65; the local WT
    had uninitialized LVGL although compiler input was explicitly external.
    Its original recipe/log/time remain separate. The corrected route uses
    the unchanged named gate in clean canonical623 with initialized pinned
    LVGL85aa60d18b3d5e5588d7b247abf90198f07c8a63. Eight reference-source bodies
    are independently byte-identical to the clean own623 WT. No provenance
    guard was bypassed or production source modified.

Interactive-two attempt, /home/sean/native-lvgl-interactive-after623.fXB54P:
  Core76024: actual0, GNU78.49/user98.31/sys23.98.
  Producer62433: actual0, GNU47.42/user84.71/sys11.52 (outer49.35).
  Reference49346: actual0, GNU21.71/user32.48/sys6.19.
  VM61617: actual0, GNU40.52/user49.70/sys7.84; controller0/QEMU0,
    elapsed40.086932s of unchanged180s budget. Independent bounded checker0.
  Actual INPUT_UI completed SUCCESS0; F2 entered Graphical UI Configuration,
    Down changed focus/help, Esc returned ABORTED to firmware, and the exact
    original pixel/action/completion oracle prefix passed. This includes the
    source-derived Selecting viewport/reference, unchanged outside pixels,
    restored pre-PREPARING strip and original splash. The unchanged controller
    observed its existing runtime marker. Checked saved tables uniquely bind
    FW0x001a0009/floor0x001a0009/8MiB and no RAM capsule, with no CHECK or SET.

Actual fresh normal Core builds precede fresh initial9 ROM packing; producer
configuration substitutes only the new payload path before packing. Packed
ELF32/64 entry/load tuples and segment bytes match the actual Core. Real
baseline NVMe/USB and ROM inputs are copied into owned VM media; no request,
capsule/NVRAM seed, CMS or legacy write grant is introduced. Caller inputs,
source/config/compiler/tool before/after checks are preserved and pass.
All recipes are supplied originals; ordinary compile output does not imply
that every compiler argv was printed. Producer/source identities and complete
tracked-source manifests bind the runs; selected18 signed source bodies are
archived for independent oracle review, not as a complete firmware export.

The bounded checker executes the verbatim original assert-setup-run visual,
action and completion prefix. It intentionally stops before the later
acceptance-profile config/manifest contract: no fake acceptance setting or
unchanged acceptance whole-suite claim. This proves one Q35 normal interactive
UI checkpoint, not default-zero F2, BootToFwUI requests, capsule writes, Linux
success, all-profile/UI suites, other boards or hardware readiness.

Original PPMs are retained. Installed ffmpeg produced PNG format derivatives;
independent Pillow checks prove exact decoded RGB equality for all4 default
and7 interactive captures. Root and author visually inspected initial form,
navigated help and clean restored logo; no obvious visual corruption.

Original-copies.tsv records each exact source path/hash. SHA256SUMS binds the
finite closure; verify.py checks copies (when originals remain), signed source
archive digests, actual config delta, saved tables and original visual prefix.
No firmware ROM, disk, extracted Core, executable, object, key or store image
is copied. CBMEM console/table/publication snapshots and framebuffer PPM/PNG
captures are intentional public observations, not firmware/media exports.
First collector attempt used nonexistent lvgl_status.h archive selector and
failed128 after receipt copying; corrected actual lvgl_ui.h binding passes.
That diagnostic was tool-output only; no fabricated raw failure file exists.
