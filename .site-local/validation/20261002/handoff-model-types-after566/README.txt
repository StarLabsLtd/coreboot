Handoff native model types: bounded proof after PR566
====================================================

Source baseline: signed d35031d7ad0420a8980654ae42b0c9919dc4e292.
Candidate: signed 3eb2dc088805f9f94887d1b1d96eeb971a0d83ed (PR566).
Only include/cdk2/system_fmp_handoff.h changes: its own stdint include and
14 native descriptive model fields become uint32_t/uint64_t. EFI_GUID and
actual coreboot wire records are unchanged. GNU x64 UINT64 is unsigned
long long whereas uint64_t is unsigned long: this is not a claim that
the language aliases are identical.

Actual author normal named handoff/coordinator/session/future-floor and
production-closure targets reaped 0 (session12073). GNU time is 25.00s,
user21.74s, system3.70s, peak74232KiB, copied named.time authoritative.
These are HOST component tests with modeled transport, not native MM
admission, firmware installation, hardware, or whole-suite evidence.

Two genuine generated configurations are retained. The original main
proof uses P1 (protected runtime/coreboot capsule/QEMU test FMP selected),
not P0. The p0/ proof uses actual default P0; profile-identities.log and
the before/after config/header fingerprints bind these identities.

Author P1 session87919 and P0 session42330 both reaped 0. Each compares
complete handoff/coordinator/session native objects at O0/O2/Os: 18 pairs
total. Each also invokes the actual importer against the existing fixture,
compares all 584 output bytes including padding, and hashes those bytes
through production CMS SHA256 with pinned BearSSL: six complete byte/
digest/layout comparisons. Region size32/alignment8, handoff584/alignment8
and every field offset are asserted. Exact object and representation
outputs are included. Scripts state the actual compiler flags and inputs.

Independent peer P1 session85808 and P0 session22396 reaped 0, repeated
all 18 objects/six representation comparisons and compared all 229
exported baseline files to signed d350. Peer elapsed time was not measured.
Peer raw parity logs and original scripts are copied without modification.

Earlier supplemental harness compile/link failures omitted the required
coreboot checksum header and real BearSSL codec objects, respectively.
They are retained as tool transcripts, not raw local files in this packet;
corrected actual exports/link inputs passed without changing production.
Packet assembly first attempted an incorrect coordinator path in git
archive; the read-only export failed and was corrected to the actual
src/modules/system_fmp/coordinator.c. No artifact success is inferred
from that failed command.

source/baseline-d350.tar contains signed common input sources/headers;
source/candidate-3eb.tar overlays only the reviewed header. Scripts retain
their original absolute receipt paths for provenance; a replay must use
private copies and explicitly adapt those paths to reconstructed sources,
generated configs and actual pinned BearSSL. No private keys, media, ROMs,
guest outputs, signing certificates, or native authority grants are here.
The tiny representation .bin files are HOST model structure bytes, not
firmware/media. files.sha256 binds all public packet files except itself.
