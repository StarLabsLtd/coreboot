Private AUTH2 runtime chain: finite public receipts after ready PR 617
===================================================================

Source and profile
------------------
Signed clean consumer 9120b63d0430af6f0239856175871022f96bc37c (ready PR
617), parent feature ad7d8e1e86d37f191d0b825e9730559b1fb7d3cb after ready
616 c702e10fcc104e7059211e4342704f06f7c11b78. The second commit corrects
only the test oracle's interpretation of real clean FTW completed history.
Original six-path feature 9a694e9ee070db9761af222215f169e2d985e2e3 after
610, and both failed attempts, remain historical immutable evidence.
Producer 389: 7ee34bed989c46913c3ee6672fb25e83227c3b6c. The archived
producer sources are the five actual FV/store/record/FTW/certdb codecs, their
12 actual producer headers, and the existing completed-history unit source.
Four source archives contain exact signed Git blobs identified by their TSVs.
The consumer archives each contain exactly the six changed paths, not an
entire build dependency tree. The historical complete source/dependency hash
receipts remain separate; this is not a standalone firmware build archive.

This deliberately uses the existing fullgraph acceptance fixture:
QEMU_ACCEPTANCE_PROFILE=1, PROTECTED_VARIABLE_RUNTIME=1,
NATIVE_SECURITY_STUB=1, NATIVE_QEMU_TEST_FMP=1, build SECURE_BOOT=0,
NATIVE_SYSTEM_FMP=0, strict direct runtime and linear boot enabled. The actual
PK/KEK/db enrollment and subsequent cryptographic verification are real.
This is NOT normal SystemFmp capsule-writer/Core, Secure-Boot-enabled build,
Linux, physical hardware, or latest-head whole-regression evidence. Here
"normal build" means the normal Make workflow, not a production profile.

Fresh actual execution
----------------------
Author session 48484 actually reaped 0. External output:
/home/sean/native-private-auth2-final-retry.mPv628/artifacts/run.wIor8J.
GNU time: wall 105.11 s, user 90.30 s, sys 21.44 s, peak 216888 KiB, exit 0.
The exact external run.sh, raw native.log/time, copied configuration, actual
generated config.h, build log, input/output/source hash receipts, four serial
logs, refusal-register logs, positive guest status files, and real codec
logs are included. The same newly built Core ELF was used across all four
VMs: SHA256 8b0e097298c6e934151ac03afbb10b69e20170bce4f122956e10cdda9de126e9.
It is byte-identical to the 615 audit Core under this SAME qualified profile;
it is not another normal-SystemFmp writer proof. Actual image/source/config
audits were not weakened and no guest code was changed after boot/signing.

The two real historical-donor negative VMs refuse before app admission:
absent endpoint -> VariableRuntimeDxe/VARIABLES_MIN NOT_FOUND, and old
GENERAL without the state predicate -> required EsrtDxe UNSUPPORTED. The
ordinary and enrolled VMs exit with actual guest status 3 after real EBS,
nonidentity SetVirtualAddressMap, physical-alias removal and runtime Get/Set.
Existing PK->KEK->db enrollment, unsigned/wrong-signer child refusal and
actual signed child admission remain in the enrolled gate.

After the real virtual transition, the enrolled private-variable chain is:
genuine signed create; wrong signer denied with value/binding unchanged;
nonappend create replay denied unchanged; same-signer later signed APPEND;
signed empty delete. Exact data and coupled certdb binding are checked at
each stage by the runtime app. There are FIVE PRIVATE_* stage markers plus
the generic terminal RUNTIME_PASS, not six private markers. APPEND replay
rejection is deliberately not asserted. Signed attributes are 0x27 for
create/delete and 0x67 for append, including APPEND_WRITE in the signed data.
The fixture independently constructs SHA256(ASCII CN "native-service-db" ||
complete encoded DER TBSCertificate), matching the real private binding
contract. expected/ contains only the public certificate and small expected
binding/certdb fixture, not private signing material or installed media.
The actual generation log's existing uninitialized-value warning is retained;
it is not silently removed or treated as a verification failure.

Real final media oracle
-----------------------
The actual producer's five codecs decode the final enrolled 64 KiB SMMSTORE:
private variable absent; certdb exact attributes 0x27, four-byte empty value,
zero authenticated timestamp and no private binding; clean FTW WORKING
workspace, no active entry and no dirty variable-store tail. A virgin EMPTY
queue requires offset 32; decoder-validated clean completed history may have
QUEUE_NONE at a bounded offset greater than 32. No hardcoded offset 592 or
producer codec relaxation was introduced. O0/O2 ASAN (including leaks) and
UBSAN passes include copied-media-only workspace, pending record, completed
history, live-private and orphan-certdb hostile cases. Those mutations never
enter a VM. The external run fingerprints all 65 actual dependency paths,
including producer and HOST/system headers, before/after. The reusable codec
shell itself hashes the five C codec TUs; do not infer header closure from
that shell alone. Codec executables, whole SMMSTORE and flash/disks are omitted.

Failures remain failures
------------------------
66547 actually reaped 2, GNU 44.77 s, before ANY VM: the strict PCI MTRR
raw-byte audit saw one 0f30 inside a MOV displacement. Ready615 adds the
existing source/hash-bound zero-decoded-WRMSR classification and mandatory
real-opcode hostile test. The original preflight/build logs are preserved;
they are not a native failure or success. The separate 615 packet documents
that prerequisite in detail.

67991 actually reaped 134, GNU 101.23 s: all four guests had passed, then the
HOST codec assertion incorrectly required a virgin EMPTY queue. Real clean
FTW history had QUEUE_NONE and completed records. Its raw assertion, all four
saved serial outcomes and source/input receipts are preserved. It is NOT
reclassified as an aggregate pass. Root and Entry reviewed the bounded oracle
correction against real classify_queue/completed_history source, followed by
saved-media checks and the entire fresh 48484 rerun. An earlier dependency-list
preflight exit 123 also remains at native-private-auth2-after610.WbpE8Z; it is
not part of the successful execution and is not repackaged as a gate.

Independent peer and replay limits
---------------------------------
Entry's saved-run-check.py/log/time reapplied all four saved outcome predicates,
exact-one marker cardinality, same Core, source/input/header/output/payload
bindings, and independently rebuilt CN+TBS binding/certdb bytes. Entry also
rebuilt the real codec oracle on fresh final media: O0/O2 strict SAN passed,
GNU 2.55 s. Earlier saved failed-run media passed the corrected HOST oracle
independently (GNU 2.39 s), without reclassifying the old aggregate 134.
These are saved/read-only HOST reviews, NOT independent fresh VMs. Root
independently read actual outcomes/source/profile and released ready617.
check-receipts.py verifies the portable finite packet/source/serial/profile
claims only; it cannot re-decode omitted private media or reproduce a new VM.
The copied peer replay and exact author recipes retain their original local
paths and need the omitted immutable local artifacts for full replay.

No private keys, executables, ROMs, whole variable media or disks are published.
Intermediate cold/cut persistence, recursive/differential hierarchy, dbx/dbt,
normal SystemFmp production profile and hardware acceptance remain open.
