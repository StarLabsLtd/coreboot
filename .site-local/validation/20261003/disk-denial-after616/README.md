# Ordinary DISK CHECK denial at ready616

This is a bounded negative-path observation, not positive DISK write admission,
guest boot success, Linux boot, controller quiescence or hardware evidence.

The normally composed Core contains real SystemFmp, protected variables and the
optional production transport diagnostic. It was compiled from ready611
`ad87ab8296a96bd59f9ed40a7f760652e617d3b9`, not rebuilt from the later whole
ready616 tree. Its ELF SHA256 is
`737d4306f0969106fc550255a7d3e888f4856ef06b233e7ec2afef478dfffd9c`.
Both actual producer389 initial9 and targetA firmware contain that Core before
capsule signing. Public DER trust is unchanged. Test FMP and QEMU acceptance
profiles are disabled. The signed target attempt is `0x001a000a`, with static
floor `0x001a0009`; genuine HOST CMS/FMAP validation succeeds.

The producer389 capability change enables existing CBTABLE DISK delivery under
the public delivery policy, without legacy UPDATE_CAPSULES, generic SMMSTORE or
full-flash grants. It does not add late writer authority or reopen the closed
early RAM mutation window.

## Actual runs and retained failures

Original run1 timed out under the original fixed 180-second deadline: QEMU
180.028431547 seconds, GNU aggregate 182.34 seconds, outer status 1. Actual
saved console already shows CHECK `BAD_RESPONSE` (enum8), no SET, and disk
phase `complete | SUCCESS`. The original observer incorrectly expected
`end SUCCESS` and its original termination string incorrectly says after denial.
Those immutable receipts remain failures; they are not relabeled passing runs.

The corrected observer adds the actual phase grammar, strict saved-console
revalidation and accurate future timeout/observer classifications. Its seven
modeled HOST cases include rejection of the original grammar. A fresh private
run2 returns outer 0: QEMU 12.460005262997583 seconds, GNU aggregate 18.06 seconds.
It is deliberately HOST-terminated after the bounded denial, not guest PASS.
Both use the same immutable Core, initial/target firmware, signed capsule and
public baseline disk; no post-signing payload substitution occurs.

Run2 records RAM complete SUCCESS before PCI, then actual DISK CHECK enum8
`BAD_RESPONSE`, no SET, and disk complete SUCCESS. The scan reports
COMPROMISED_DATA; do not infer local CLOSED or a typed authentication refusal
from that public error. The source capsule remains byte-exact, firmware outside
SMMSTORE and the staged disk remain unchanged, and the genuine producer codec
checks canonical zero FmpState/attrs3 and clean FTW at O0/O2 with sanitizers.
All 24,981 input mappings agree before/after the actual run. They were checked
against live inputs after reap before publication metadata; this packet's
checker does not invent a later fresh preboot manifest.

Bounded Q35 ECAM command observations show NVMe 2→6 (BME enabled), xHCI 2→7
(BME enabled), and EDU 2→2 (BME disabled). These observations are not device
drain, DMA revocation or safe late-writer admission proofs.

The earlier genuine producer build attempts failed with unsupported absolute
Mbed TLS object mapping; their logs remain distinct. Corrected builds use the
normal relative pinned Mbed TLS mapping. No warning/ownership audit is disabled.

## Source and open behavior

Original source `abae9576`, corrected source `f76c410602`, and ready616
`c702e10fcc104e7059211e4342704f06f7c11b78` are separately archived. The last
two two-file bodies are identical; the ready616 parent is actual615
`90a8821c5e9a99d4bb4768268d1a0cda9fe3edf7`.

This run observes the existing capability-gated scanner without a genuine
OsIndications request. Standard request gating/consumption is a separate next
functional slice, not supplied by HOST seeding. Positive DISK-origin writer
sequencing and controller/source ownership remain open. A next-boot retained
RAM route must require the actual advertised persistence capability and use
the existing early authority; this packet does not authorize a late grant.

Only source, public configuration/inventory, logs, bounded CBMEM console/table
captures, hash mappings and scripts are copied. Firmware, disks, capsules,
variable-store contents, binaries and keys are not included. External artifact
digests describe actual retained local artifacts, not redistributed assets.
