PCI bus executable-byte displacement audit prerequisite (ready PR615)
==================================================================

This finite packet records a build/audit correction, not an AUTH2 native,
whole-regression, hardware, or power-cut result. It contains no ELF/PE binaries,
ROMs, disks, authenticated capsules, executable tools, keys, or TPM state.

Source and cause
----------------
The exact signed ready source is 90a8821c5e9a99d4bb4768268d1a0cda9fe3edf7,
parent 93e39d5a98547a7cd7028fdc7b7f38e3a2d855a7 (PR614). One src/boot/Makefile
change adds 17 lines: the selected PCI bus image uses the existing
false-positive|auto classification, and the selected-image audit must execute
the existing parameterized direct ownership fixture for that PCI image.
The auditor, manifest generator, section parser, and causal fixture are not
changed. Root and Style independently read and accepted the complete source
change and existing strict audit/causal code.

The observed PCI image is SHA256
bafaf3fc91952bd0028a00eb908ddb7d8a3233bcf946da57f1764455e6b79924.
Its raw executable section contains one 0f 30 pattern, in the displacement of
the instruction at RVA 0x6482:
  48 89 15 0f 30 00 00    mov %rdx,0x300f(%rip)
There is no decoded WRMSR instruction. The fresh image is byte-identical to the
preserved original failed image. The classification derives the canonical
executable raw count, binds the actual artifact SHA, and requires zero decoded
WRMSR instructions. It does not allow a writer or relax an expected count.

Actual receipts
---------------
1. Preserved original AUTH2 source build: session66547 actually reaped2 before
   any VM, GNU WALL44.77 USER40.07 SYS5.87. The actual six test-only changes were
   on PR610 source336c987ea934141a3860fa31f45eb0aaa17435ae; they were later saved
   unchanged in signed WIP9a694e9ee070db9761af222215f169e2d985e2e3. The raw build
   stopped because the old PCI policy expected raw count0 but found1. The native
   application compilation success is not a native application execution claim.
   Original directory /home/sean/native-private-auth2-after610.FeimaI,
   nested artifacts/run.ZKioUc. The failed-auth2-build subdirectory keeps its
   original logs, config, manifests and count0 policy unchanged. The source and
   actual complete producer-codec header closure checks passed after that failed
   invocation. Those codec sources were not executed by this failed native gate.

2. Fresh normal prerequisite build/audit: session90285 actually reaped0,
   GNU WALL117.07 USER96.28 SYS31.79 PEAK_KIB99496. Actual recipe is fresh/run.sh;
   original directory /home/sean/pci-bus-mtrr-audit-after614.kFd3bH. It copies the
   previously resolved fullgraph config into a new owned build and uses normal
   native-coreboot-image plus native-direct-mtrr-ownership-audit targets, genuine
   producer389 config/source and pinned BearSSL. No objects or generated headers
   are seeded. Source was the exact PR614 tree plus the frozen one-file candidate;
   it was signed as the above PR615 commit after execution. All1600 tracked regular
   source hashes match before/after; the recorded config/compiler/tool/recipe
   inputs and candidate patch match. The normal full Core ELF layout and strict
   ownership audit pass, including all four selected parameterized direct audit
   fixtures. The PCI policy is exactly false-positive|1.
   Resulting Core SHA256:
   8b0e097298c6e934151ac03afbb10b69e20170bce4f122956e10cdda9de126e9.
   These quiet Make logs are not represented as a complete original compiler
   argv transcript. The actual top-level recipe and signed Make sources are kept.

3. Original artifact-only replay: session64979 actually reaped0,
   GNU WALL10.22 USER8.05 SYS3.61. Original-artifact/audit.sh builds a new bound
   manifest for the untouched failed-build artifacts and runs the unchanged
   strict auditor and PCI parameterized fixture. Zero/raw-nonzero policy cases,
   wrong count/class cases, and a same-raw-count actual WRMSR replacement all
   behave as required; the real instruction is rejected even after its SHA is
   refreshed. All recorded artifact/tool/script input hashes match afterward.
   This is a saved artifact audit, not a recompilation or VM execution.

The narrow project patch checker reports zero errors and warnings,29 lines;
this does not close the broader repository lint work. No compiler/auditor or
policy source was weakened to obtain these results. A later AUTH2 native run
must use its freshly compiled exact final source and is outside this packet.

Reproducibility and scope
-------------------------
Run python3 check-receipts.py for finite packet integrity/source-blob and raw
receipt checks. files.sha256 covers the finite packet files except itself.
signed-audit-sources.tar.zst contains exactly seven regular Git source members,
with their Git blob IDs and SHA256 in signed-audit-sources.tsv. The original
absolute-path manifests and recipes are retained as captured; they do not
pretend to relocate all build inputs into this packet. They refer to immutable
external source/config/artifact inputs and contain hashes, not private contents.
This packet permits offline saved-receipt checking, not a fresh build without
the separately retained real dependencies. Re-executing fresh/run.sh writes its
original owned output and is not the finite packet checker.
