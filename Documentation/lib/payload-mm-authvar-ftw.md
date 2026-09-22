# Read-only authenticated-variable FV/FTW recovery planner

`PAYLOAD_MM_AUTHVAR_FTW_DECODER` is a default-off, SMM-only decoder for the
EDK2 26.09 firmware-volume and fault-tolerant-write layout used by coreboot
SMMSTORE. It derives the variable, working and spare ranges from the complete
region and logical block size. Three blocks are the minimum. The spare half is
at least as large as the variable range and one block is the workspace.

The decoder validates the system-NV FV header, block map, checksum,
authenticated variable-store envelope, FTW workspace GUID and CRC, fixed-width
write queue, the three pinned 26.09 DXE/SMM/StandaloneMm caller identities, the
stable coreboot authenticated-variable caller identity
`C4A52EF3-4E27-47E2-950B-FDAAB521B895`, and
the exact variable-reclaim target. It returns only a typed recovery
plan: clean, initialize a provably erased workspace, discard an uncommitted
stage, reclaim a dirty or exhausted queue, abort before a spare was committed,
replay a validated spare, complete an already written destination, or restore
a valid workspace copy with an explicit empty/abort queue disposition. A
separate spare-cleanup action is emitted before CLEAN when the authoritative
active FV and a clean or reclaimable working queue prove that the spare is
stale transaction residue. The spare must be either a complete valid exact copy
of the active FV or a bitwise NOR erase-direction descendant of the active FV;
this includes a reset partway through erasing one byte. Any surplus spare range
must already be erased. An invalid spare alone never authorizes cleanup. The
cleanup plan carries the complete geometry spare range, zero queue coordinates
and no queue disposition. Its executor must erase and verify that whole range,
stop on any media failure, and re-read and re-decode to the underlying CLEAN or
RECLAIM_WORKSPACE result before binding cache or starting another transaction.
Any contradictory result fails closed. An uncommitted empty-workspace stage is
discardable only when every observed bit is on the NOR programming path from
erased media to the canonical body for the decoded geometry; this includes a
reset partway through programming one byte. The erased state byte alone is not
proof. Ambiguous, malformed, truncated or out-of-range media fails closed.

This library has no flash callback, erase or program operation, endpoint,
coreboot-table producer, SMI route, CDK2 caller, or persistence claim. A later
gate must execute plans through a protected SMM-only backend with readback and
power-cut tests. It must keep ordinary append-state recovery separate from FTW
reclaim and must not weaken the deliberately strict inner-store scanner.
When restoring the working block, that executor must program an image whose
header allocation and completion bits are both still erased, then commit the
header by clearing its allocation bit only after the complete block write has
succeeded. Therefore a prefix-programmed primary image remains in state `0xff`
and can be restored from the valid spare copy. A synthetically committed
`0xfe` header with a malformed queue is corruption and fails closed; recovery
must not hide it by falling back to the spare workspace.

The pinned field offsets and source blob identities are recorded in
`tests/lib/payload_mm_authvar_ftw_edk2_2609_layout.tsv`. Host tests cover O0,
O2, strict warnings, ASan, UBSan, geometry boundaries, valid recovery states,
completed queue history and hostile mutations.
