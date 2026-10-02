# Normal Core RAM/SystemFmp update receipts

This packet preserves the actual first successful production normal-Core RAM
update and its independent fresh replay, together with the earlier extractor
preflight failure and genuine provider/request alias failure. Restore and native
negative results are separate work and are not established by this packet.
Version shorthand `9`/`a` means the full values `0x001a0009`/`0x001a000a`.

## Exact sources and admission scope

The successful consumer source is signed
`ea1b13a7479e22bb5db908142197813ce21f940b` (PR573), including the normal fixture
from PR571. The producer source is signed
`b06f91d78a891e1d803ea48543bdf545231ef786` (PR386). The actual normal Core ELF
is `e9d1c3166e5e16b7949af58d393cabfbfcd2bdb56fecb1218155b20b6cc00622`.
Both initial and target firmware were packaged with this complete Core before
signing. Configuration and direct module inventory require SystemFmp enabled,
QEMU_TEST_FMP disabled, real protected VariableRuntime and P1 admission.

The ordinary MAIN application is loaded through the normal boot flow. Actual
SetupMode=1/SecureBoot=0 is asserted; this is not an enrolled Secure Boot test.
The genuine signed full capsule is retained through real ReservedMemory scatter
and UpdateCapsule with PERSIST/INITIATE_RESET, not a synthetic MM grant or direct
reset fallback. Two observed guest resets establish three independently saved
firmware epochs: compiled 9, compiled 9 with actual capsule handoff, compiled a.

The warm saved bounded capsule bytes compare exactly to the validated signed
input; their private external binary is not copied into this packet. Full saved
LBIO tables, CBMEM console data and UART are copied. Normal epochs require actual
RAM success/end before PCI; the warm installed epoch has no PCI phase. Successful
CLOSE before its cold reset is inferred from the reviewed real Core source path,
not a separately observed CLOSE marker. The cold ordinary MAIN checks the actual
descriptor and five-word FmpState: running a, durable history/last attempt a,
floor 9, success status and all four present flags.

## Actual outcomes and timing

| Receipt | Outer status | GNU wall/user/system seconds | QEMU seconds |
| --- | --- | --- | --- |
| author79302, `author-positive` | 0 | 174.77 / 171.47 / 13.77 | 171.507427259 |
| independent49676, `peer-positive` | 0 | 152.43 / 148.83 / 11.53 | 149.280842 |
| earlier67045, `failed-provider-alias` | 2 | 184.12 / 45.10 / 5.51 | 180.8866 |
| earlier24286, `failed-elf-preflight` | 2, before QEMU | see original GNU receipt | not started |

The predeclared three-boot QEMU deadline is 180 seconds, unchanged on all normal
runs. Successful runs exit through real guest status 3, with two guest RESET
events and no recorded failure. GNU aggregate includes preflight/app build.
Independent49676 uses its own private media/config/output and app compile, with
an explicitly copied actual e9d1 Core/inventory; it is not an independent Core
rebuild claim.

The earlier extractor assumed ELF64 although actual cbfstool emits an ELF32
container. Its narrowly corrected decoder compares actual entry and all loaded
virtual/physical/file/memory spans and bytes to the ELF64 Core; reconstructed
flags/alignment and whole ELF file identity are not claimed.

The earlier warm production failure returned EFI_INVALID_PARAMETER from
CAPSULE_RAM. Real provider passed its own session-member GUID into a session
anti-alias guard. PR573 copies that GUID locally; every actual guard and auth
policy remains intact. The retained capsule from that failed run separately
passed exact signed-byte/CMS validation. Its deadline expiration is preserved,
not reclassified as success.

## Readback and limitations

Actual host readback compares the complete COREBOOT route to its signed pristine
target. The excluded SECURE_PROBE and adjacent gap remain unchanged. The private
NVMe image remains unchanged after the prior ordinary MAIN insertion. Protected
SMMSTORE may change through authorized real variable/checkpoint writes and is
not asserted byte-invariant. Before/after maps bind actual source, caller config,
compiler/linker/PE tools, capsule tools, Core/inventory, images/app/capsule and
public certificate. These maps are the original captured before/after receipts
for each frozen run, not a claim that later edited source still matches old
maps. Images, disks, private signer keys and variable-store media
are not copied here; external artifacts are hash-only.

This is actual QEMU normal production capsule-route evidence, not hardware,
power-loss durability, normal restore, native wrong-signer/replay/rollback
coverage, or completion of the wider firmware roadmap.
