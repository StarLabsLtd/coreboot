Normal certified RAM signature-refusal continuation after PR597
2026-10-03

Accepted slice
--------------
Three real QEMU invocations reached guest exit 3 with failure=null under the
unchanged 180-second class: root wrong-signer79078 and signed-byte86230 in
/home/sean/normal-refusal-consumed-producer.VPNIdB, and fresh independent
wrong-signer82744 in /home/sean/certified-refusal-continuation-peer.Js7GPg.
The native logs, original GNU time files, actual result/QMP events, saved
checked CBMEM tables/consoles, command and input maps are copied verbatim.

                        QEMU seconds          native GNU seconds
root wrong signer       73.52133767299892       75.59
root signed byte        73.48900207000042       75.49
fresh peer wrong signer 71.73265750599967       73.68

The peer whole app-compile-plus-VM invocation was also actually status0,
GNU75.18. Root case status0 comes from the recorded reaps79078/86230;
peer82744 was independently reaped0. Time files and result.json are original,
not durations inferred from file mtimes. Reaping evidence is tool observation,
not a field invented in result.json.

Each case has exactly two independently accepted firmware-version9 epochs
(0x001a0009), one real guest warm RESET and one retained capsule handoff.
The warm epoch contains the exact source-issued Core signature-refusal line,
CAPSULE_RAM complete SUCCESS before PCI_ROOTS, and no failed RAM phase.
The ordinary normally loaded MAIN app reaches its continued marker, checks
that CapsuleUpdateData is absent and real FmpState remains the canonical
20-byte zero record with attrs3. It does not restage or perform SET.
The flash oracle compares the entire 8MiB image outside the actual SMMSTORE
(offset0/size65536) with the genuine initial producer image. The real producer
variable codec independently verifies clean FTW/FV and namespace/name/attrs3/
20-byte canonical zero state. Its O0/O2 ASAN+UBSAN logs are copied; this is
the codec oracle, not a new cryptographic authenticator or seeded store.
The runner enforces full pre/post NVMe equality. Saved readback scripts verify
the recorded final disk hash; they do not reconstruct an unrecorded initial
post-staging disk hash.

Source and immutable input binding
---------------------------------
PR597 source is signed36d58d744f87d844b1ed7599717d06569dbac9a8 after signed
59645878c4c274142a215c9021d6515a3867362da02. source-after597.tar.gz contains
exactly the six changed regular Git bodies (including ROADMAP); the TSV binds
each full commit, Git blob, SHA256 and path. Native fixture source was the
clean signed31e3a5bae19db98457bfbd61fb1b37ed6b0ade52 checkout, with identical
functional five fixture bodies and terminal-phase guard after restacking.
The saved source-head/status files remain31e3; they are not relabeled597.
The fresh peer used its separately pinned detached clean31e3 worktree.

The actual normal Core is the genuinely compiled PR5956259 ELF93d758092e214f19480f3fabcaacc9c3f8a48a06b1107911e117338c753d6d64.
It is reused, not a freshly compiled whole PR597 payload. The checked loaded
ELF tuples from both CBFS images equal this actual Core; their ELF32 container
hashes differ from the original ELF64 but loaded bytes/address/entry match.
Producer source is signed coreboot387d960d256e2c3c14ff4aa46d6443ba20a2fab77f7.
Both genuine9/A producer builds embed that SAME actual Core before capsule
generation/signing. Original producer recipes/configure/build logs, before
hashes and output manifests are retained. external-input-hashes.tsv records
actual Core/ROM/tool/app/capsule/retained bytes hashes WITHOUT copying them.
Producer config/header and native inventory prove SystemFmp selected,
QEMU_TEST_FMP=0, QEMU_ACCEPTANCE_PROFILE=0 and protected variable runtime.
Producer trust remains the genuine configured public trust; the wrong-signer
public cert is only a HOST fixture-verification input, never producer authority.
No private signing key, trust swap, seeded admission or post-write Core
substitution is used. MAIN apps were normally compiled/staged before boot.

All three native input-before/input-after maps compare exactly and preserve
original absolute paths. They are captured contemporaneous maps, not freshly
invented prebuild manifests. Root source paths have since been restacked for
publication; archived historical maps must not be described as current-path
hash equality after that move. The saved pair replay checked all original
paths while they were still frozen. The pinned fresh peer additionally checked
all original current input hashes after its real reap; peer-source-before
still matches its script and three actual source inputs.

Independent checks and preserved failure
---------------------------------------
peer/saved-replay-final.* records saved-only opposition0 for the root pair;
it is not a new VM. The earlier saved-replay.log/time preserve a loader-only
status1 caused by the missing actual tests import path. Corrected final replay
uses the real runner and records its saved-only qualifier. fresh-readback.*
is saved-only readback of the separately reaped fresh82744 VM. saved-codec.*
is a further actual producer-codec check80717, not a fresh VM or causal mutant
replay. check-receipts.py validates finite saved boot/marker/map/archive
bindings without guest writes and can optionally compare original raw files.

Earlier actual180-second failures87638/11815 remain distinct in evidence
commit21216ea926cd9612a0615607fe181a96295d9afe, at
.site-local/validation/20261003/certified-refusal-native-failure.
Those used old Core593d386, reached RAM SUCCESS then PLATFORM_TABLES
COMPROMISED_DATA, and expired. The reviewed595 completion fixes protected
RAM selection to NONE only after certified CLOSE, exact request retirement
and postchecks. It never relaxes stale-RAM validation or permits disk retry.
The later terminal-phase observer guard is not retroactively applied to those
old180-second failures. Their original raw evidence is referenced, not replaced.

Limits
------
This proves the bounded normal production Core RAM signature-refusal
continuation slice under shipped QEMU, not Linux boot, hardware, full-tree
exact597 compilation/regression, below-floor recovery, standard CapsuleReport/
CapsuleLast publication or FMP failed-update history. CLOSE and exact request
retirement are source-backed and the MAIN app observes request absence;
there is no invented direct CLOSE wire marker. Signature rejection is not
authentication success. Generic postguard/CLOSE/retirement faults remain fatal.

Only public logs, recipes, configs/headers/inventory, input hashes, saved
CBMEM proof and six source bodies are included. No ROM, disk, retained or
signed capsule payload, variable-store contents, executable/object, socket,
TPM state, private certificate, key or token is copied.
