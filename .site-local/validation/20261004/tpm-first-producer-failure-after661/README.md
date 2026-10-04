# First TPM producer build failure after Ready661

The original attempt at tpm-acceptance-build-after661 failed before payload
compilation and before any VM: producer-build raw status2 and aggregate2.
Source/tools before-after snapshots compared successfully. This is preserved
failure evidence, not a passing firmware, TPM, guest or hardware gate.

Coreboot's vboot include fixup prepended its source root to an absolute
VBOOT_SOURCE include, producing an invalid source-root//home/... path.
The separately reviewed replacement outer recipe computes a producer-relative
path and checks that it resolves to the same canonical hash-pinned vboot source.
Its presence here is source-review evidence, not proof of a successful retry.
Upstream vboot remains an approved unsigned hash pin; no vendor signature is
claimed. The freezer's old empty-diagnostics note remains historical context.

Only immediate-root failed-stage text/config/hash/status receipts and the eight
explicit original/replacement draft files are copied. No partial build objects,
payload/native trees, source/vendor traversal, EFI/ELF/ROM/raw media or active
relative-build attempt is included. Original failures and sources are untouched.
Large text uses deterministic lossless gzip with decompressed byte comparison;
the original and stored hash ledgers bind their respective bytes.

Root alone executes the reviewed collector and commits/publishes evidence.
No collector execution or new gate is claimed by this prepared source.
