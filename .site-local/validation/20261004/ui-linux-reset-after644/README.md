# Bounded normal UI cancellation and Linux-origin setup reset

These are retained normal628 firmware receipts, not a fresh joined-head Core
build or hardware signoff. Firmware source is signed `d8e49af2ab0c`; producer389
is signed `7ee34bed989c`. Ready CDK2 PR643 incorporates the UI caller fixes and
PR644 incorporates the Linux-origin reset route; neither rewrites the firmware
used by these earlier guests.

The Linux `j6OTbN` guest finishes in 85.115 seconds within its original
180-second limit, with wrapper/QEMU/observer statuses zero. Linux creates and
updates its probe, requests firmware setup through Runtime services, and resets
itself. Checked guest RESET and both handoff/splash epochs precede setup entry;
Down changes help, Esc restores the logo, and the second Linux verifies
persistence, deletes the probe and powers off through S5. Independent `Qgs18N`
checks all 33,708 input bindings, saved history, four actual frames and genuine
producer whole-store O0/O2 sanitizer results. No host reset supplies this route.

The UI `eAhimP` guest finishes in 42.136 seconds with wrapper/QEMU/observer
statuses zero, but its caller aggregate remains 1 because the post-guest
manifest expectation incorrectly required boolean false instead of integer
zero. The actual checkbox starts unchecked; Right checks it and enables the
dependent controls; Down selects Height and changes help; Esc restores the
logo before Linux and S5. Signed observer `6e4405633cda` corrects only the typed
expectation. Qualified saved-only `AKe0XW` admission, input bindings, UI oracle
and genuine whole-64KiB producer media checks pass at O0/O2 with sanitizers;
the comparison mutant fails as intended and the inverse check passes. Escape
does not persist an LVGL settings record. No second guest is claimed, and the
original caller failure is not reclassified.

The failed objcopy-input, single-QMP-endpoint, epoch-slice and UI Kconfig
attempts are retained in separate directories. The corrected ready644 HOST
union, both saved histories and whole Linux-media checks are also included.
These checks do not close enrollment, Selecting/save persistence, capsules,
TPM, controller-drain, final-head or hardware gates.

The collector copies only selected local text receipts/configs/scripts and
actual PNG derivatives. Large JSON is gzip-compressed losslessly; independent
review compared all 443 selected files, including twenty compressed JSON,
against their originals. `ORIGINAL_FILES.sha256` identifies those original
paths. This is a finite receipt archive, not a self-contained executable replay:
raw flash/disk media, firmware binaries, executables, PPMs and imported source
are omitted. The original media hashes and frame identities remain in the
saved manifests/input maps. No private keys are included.

A host reboot interrupted the first archive copy; that partial copy was moved
aside, not deleted or counted as complete. The fresh collector finished with
exit zero. Native and saved validation results above predate that reboot.
