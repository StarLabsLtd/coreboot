PR707 selected-boot UKI and disk artifact qualification — 2026-10-05

Source is signed CDK2 c1dba725d3734bb1f737c6ec08b4cec68e16967e.
The archive retains the actual selected UKI, its manifest, the packaged
134217728-byte derivative disk and its manifest, and literal build/selftest
logs, statuses and timings. The canonical input disk is not overwritten.

Selected UKI SHA256:
897b8c36109e4c2a9a33cba771e21e293f3b7bb6e16f99a29bd3cbe06c1a7662
Selected disk SHA256:
ba34d9de4c762bf8908d9256d31d50cb01632e513145c4d52a0605af184c37a6
Canonical disk SHA256:
054a451e67b291adc7390301ef21c9df2de3582e768612cc66459e58d1305e36
Expected Boot000A/B/C, BootOrder and BootNext data comes from the signed
source fixture, not an observed guest store. Its JSON SHA256 is
82cf7d3a2adf6b7412e8b8bcfed69d5c3242224b25a8dda06b04e30c859d9b12.

Actual builder inputs are:
/home/sean/current693-fwui-qemu.PMINOC/selected-tpm-pinned-inputs/kernel
/home/sean/Documents/cdk2-validation/issue454-pr340/fixture/root/bin/busybox
/home/sean/Documents/cdk2-validation/issue454-pr340/fixture/root/bin/cbmem
/home/sean/current693-fwui-qemu.PMINOC/selected-tpm-pinned-inputs/stub
The kernel and stub were copied from recoverable Trash after independently
checking the existing immutable pins; no replacement kernel or seed was
invented. The builder manifest records all four exact input hashes.

Independent structural review checks PE64 section bounds and hashes,
the exact selected command line, unique five-variable CPIO data, executable
helper/init/busybox/cbmem bytes, literal source bindings and fixed epoch.
The disk contains the exact UKI at EFI/BOOT/BOOTX64.EFI. GPT metadata remains
byte-identical to the canonical input. Actual reproducibility selftests
pass for differing input paths and mtimes and include rejection checks.
Temporary negative artifacts were removed by the existing selftests;
the retained logs do not claim separate per-negative execution receipts.

The first packaging selftest intentionally remains recorded as failure1:
Root initially supplied the obsolete issue454-pr340 nvme.raw (SHA63deb),
which the immutable canonical SHA check rejected. The corrected canonical
selftest is a distinct log/status/time, not a rewrite of that failure.

This closes artifact packaging and bounded HOST checks only. No guest has
executed these selected variables, no selected BDS/BootCurrent/PCR1/4/5
hardware or QEMU acceptance is claimed, and the attempted TPM-enabled
producer build failed on absolute vboot include-path handling. That build
failure is retained separately and remains an open gate.
