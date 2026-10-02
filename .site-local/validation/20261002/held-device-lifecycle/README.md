# Held controller I/O: bounded actual QEMU lifecycle receipts

Test-only CDK2 checkpoints `348fc9fdb87e9e514b91641174cfe1239143098e`
(NVMe/xHCI) and `86c61c6823ad3fe031c3f53d784277b643ded97a` (onboard AHCI)
are signed and pushed. Root and independent peer read the complete source.
The final AHCI-capable coreboot helper is signed checkpoint
`dd78f90e20fe628b9d502950973a9483d00f31e5`; its C SHA256 is
`383beca82fbca36be6ea2b17265baf3a56ff3cba14e0a96051daadd8182b159a`
and header SHA256 is
`78e58fd5e186992ecbbc540c7a857f812a77b08ce61b2ac83870b614f1028e94`.
The three additive archives here preserve exact source/helper snapshots and
actual binary/input hashes; earlier archive hashes are not rewritten.

The shipped QEMU 10.1.0 Debian binary is exercised through real PCI/MMIO and
controller queues using qtest, a generated CLI/HLT BIOS and a read-only synthetic
NBD backend that holds an actual backing read. No coreboot producer, firmware
authorizer, guest OS, or writer is substituted into these tests. This is actual
device lifecycle evidence, not coreboot/SMM current DMA ownership, VT-d physical
target exclusion, capsule authorization, firmware installation, or hardware proof.

All six final root scenarios exited 0: BME-only and reset for NVMe, xHCI and AHCI.
Unit `cdk2-root-held-io-six`, invocation `c1632102cb584cda96cc8122c888dd80`,
was actually reaped 0; unit runtime 3.315s, CPU 1.855s and peak 40.6M.
The root run has no outer tee log: its exact six GNU-timed scenario logs and
QEMU traces/commands/input hashes/results are in the root archive, and the
unit journal separately records all six PASS labels. Individual GNU timing
is distinct from unit timing. Author and independent peer invocations also
passed, without inferring unmeasured elapsed or CPU values.

BME clearing alone leaves mapped reads alive. NVMe CC.EN reset and AHCI GHC.HR
wait for those reads to drain before returning; completed bytes may reach the
old target during reset. xHCI halt likewise leaves the transfer alive, whereas
HCRST cancels the same asynchronous USB packet and preserves its target. Reset
submission events and actual QEMU traces bind reset-before-completion/cancellation
ordering. Complete 512-byte comparisons and a subsequent 100ms target rewrite
check passed; the latter is a bounded no-late-write observation, not a universal
device guarantee. Exact cancellation helper bytes are compiled and hash-bound,
but no SMM caller or admission decision is installed by these checkpoints.

Failures remain truthful: early release-path deadlocks, export/setup mistakes,
an unpropagated backend-thread assertion despite an outer exit 0, and an initial
AHCI untouched-target expectation that incorrectly rejected genuine draining.
Final callback/backend failures propagate to test exit status. Source snapshots
were collected after testing; per-scenario before/after input hashes bind them.
Unix sockets are intentionally excluded from archives and regular-file manifests.
No private keys, live firmware images, or actual variable/disk media are included.

Cleanup/release boundary: keep active producer/helper source paths and raw
receipts until these public archives are durably committed and their exact
signed source identities verified. Idle worktrees alone are not deletion
authority. Current protected DMA admission, RAM-window native closure, disk
controller lifecycle, authenticated newer-firmware installation/restore, and
fresh hardware/release review remain separate open gates.
