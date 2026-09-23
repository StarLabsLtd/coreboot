# EDK2 26.09 FTW byte fixtures

These 12 KiB images were emitted by the unmodified `FtwAllocate()` and
`FtwWrite()` implementations from StarLabsLtd/edk2 `release_26.09` commit
`aab7b589fc59b7e2b8fb7eb79519bf1a5e5a5272`. The host capture harness uses
the pinned EDK2 headers, structures, workspace initializer, workspace writer,
state updater, workspace reclaimer, and FTW transaction code directly. Its
headers come from a temporary archive of that exact commit, so checkout dirt
cannot affect the capture. Its fake FVB records the
durable bytes after each real callback boundary; it does not reproduce the
format with host-native structure dumps or duplicate the transaction logic.

Run `tests/lib/payload_mm_authvar_ftw_edk2_capture.sh OUTPUT_DIRECTORY` with
`EDK2_2609_SOURCE` pointing at the pinned checkout to regenerate them. The
script rejects any other commit or source-blob identity before compiling.

Pinned EDK2 source blobs:

- `MdeModulePkg/Include/Guid/SystemNvDataGuid.h`:
  `9bfd8fafc75354bc79cd7a43f6df3c6b00f3ceba`
- `MdeModulePkg/Universal/FaultTolerantWriteDxe/UpdateWorkingBlock.c`:
  `caa87e95d529e83b4ff7523fd556dd921e1f0864`
- `MdeModulePkg/Universal/FaultTolerantWriteDxe/FaultTolerantWrite.c`:
  `10a67767ebc58f99fad33fffe9d780631c6be6c0`
- `MdeModulePkg/Universal/FaultTolerantWriteDxe/FtwMisc.c`:
  `859d8ba5ee6bb11a091c216f0d41fdf6bf17a986`
- `MdeModulePkg/Universal/FaultTolerantWriteDxe/FaultTolerantWrite.h`:
  `6e3a24b2e63b4e937dcaaa19e34a208bbed314e5`
- `UefiPayloadPkg/SmmStoreFvb/SmmStoreFvbRuntime.c`:
  `fca063403d9f6f4c594cf5c002e138f830bb06e8`

The pinned X64 geometry is three 4096-byte logical blocks. The variable FV is
at offset 0, the working workspace at 4096, and the spare at 8192. The FV
header is 72 bytes, the authenticated-variable store is 4024 bytes, and the
queue header/record start at workspace offsets 32/72. The queue uses EDK2's
DXE caller GUID. Each image is the exact durable state after the named
boundary:

| Image | Boundary | Authority | Expected decoder action |
| --- | --- | --- | --- |
| `r0-empty-carried.bin` | committed spare workspace, empty queue | spare workspace | `RESTORE_WORKSPACE/EMPTY` |
| `r1-abort-old-carried.bin` | committed spare workspace, allocated old queue | spare workspace | `RESTORE_WORKSPACE/ABORT_OLD` |
| `t0-clean.bin` | clean workspace | primary FV | `CLEAN` |
| `t2-header-fe.bin` | header allocated | primary FV | `ABORT_OLD` |
| `t3-header-fc.bin` | writes allocated, record erased | primary FV | `ABORT_OLD` |
| `t4-record-ff.bin` | record body, state `ff` | primary FV | `ABORT_OLD` |
| `t5-record-fd.bin` | full spare committed | spare FV | `REPLAY_SPARE` |
| `t6-record-f9.bin` | destination committed | primary FV | `COMPLETE_NEW` |
| `t7-header-f8.bin` | transaction complete, stale spare | primary FV | `CLEANUP_SPARE` |
| `t8-spare-erased.bin` | stale spare erased | primary FV | `CLEAN` |

`SHA256SUMS` makes any fixture drift explicit. The interoperability harness
regenerates and compares all images before passing them to the production
decoder. It also captures coreboot's real pre-`fd`, `fd`, `f9`, and `f8`
executor boundaries. The pinned EDK2 `WorkSpaceRefresh()` consumes every
captured queue, and its `FtwRestart()` replays the `fd` image to the exact
captured spare image. An independent literal layout oracle checks the complete
FV and variable-store headers, working-header CRC, queue fields, authority
images, and erased queue remainder. Single-field mutations of every committed
queue field and both spare headers must be rejected.
