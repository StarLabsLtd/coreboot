Normal Core RAM capsule refusal receipts after PR580
===================================================

Scope
-----
Signed test-only consumer 66bd61586bce3b52833feb37ece25374cea5bc1f (parent
ca7596728d9d25a23d1cbb027ec5b298c98cb719, PR579) adds finite wrong-signer,
signed-byte-corrupt and signed-below-floor native cases. PR580 is ready at
https://github.com/StarLabsLtd/cdk2/pull/580 . Eight changed paths alter only
test fixtures, observers, HOST oracles and their actual Make recipe. Firmware
policy, public trust, provider, Core and MM grant implementations are unchanged.

Each genuine ordinary MAIN app runs late in normal Core, reads real metadata
and zero durable state, then calls actual Runtime Query/UpdateCapsule with
PERSIST and INIT_RESET. One real guest warm reset produces the second normal
Core boot; its checked handoff contains the exact retained candidate bytes.
The normal SystemFmp route has QEMU_TEST_FMP=n, acceptance profile=n,
SystemFmp=y, protected variables=y, P1 and actual producer attestation. No
first-stage override, direct MM grant, fabricated authentication success or
test FMP provider is used.

The observer requires two exact FW9 epochs, one guest RESET, the declared
CAPSULE_RAM refusal, no warm PCI phase and no cold-success marker. It then
deliberately terminates QEMU from the HOST. qemu_status=0 is cleanup success,
NOT a guest debug-exit success. These receipts prove bounded refusal, exact
retained bytes, canonical zero durable FmpState and unchanged firmware outside
the genuine SMMSTORE region. They do NOT prove rejected-request retirement,
continued boot/OS boot, a fresh independent negative VM, power-loss safety,
hardware DMA/SPI authority or a current whole-suite pass. CLOSE before the
failed RAM phase is source-backed normal Core flow, not a newly observed CLOSE
marker. The generic DEVICE_ERROR reply cannot identify an internal auth cause.

Actual native outcomes
----------------------
All source and caller inputs were held frozen until each actual reap. Original
artifact root: /home/sean/normal-core-ram-refusals.oIoDHH . Each case uses an
owned output, pflash copy and staged NVMe copy; no shared media is written.

case          actual tool cell  GNU outer elapsed  exact warm RAM status
wrong-signer  53239, exit0      43.99 s            DEVICE_ERROR 0x8000000000000007
signed-byte   19041, exit0      44.05 s            DEVICE_ERROR 0x8000000000000007
below-floor   42796, exit0      41.13 s            UNSUPPORTED  0x8000000000000003

Native .log/.time and result.json are copied verbatim. result.wall_seconds is
40.942464764 / 41.118619456 / 38.204456092 respectively; those intervals include
the final HOST producer-codec compilation/checks after QEMU termination and
are not QEMU-only timing. The predeclared live VM budget remains 180 seconds.
GNU outer also includes the normal app dependency/preflight. Each result has
failure=null, expected_refusal matching its case, refusal_observed=true and
completion='HOST termination after validated refusal'. Actual tool reaps are
recorded here from the original execution results, not inferred from a log.

Recorded before/after JSON maps are exactly equal: wrong-signer 17,110 entries,
signed-byte and below-floor 17,109 each. They bind actual source files, config,
header, app, historical Core ELF/inventory, candidate, unchanged public trust,
producer config/tool, compiler/linker/PE audit and QEMU. No before-map is
invented after execution. Runtime preflight compared real loaded ELF segments
of BOTH pristine firmware payloads to the supplied genuine full Core; native
32/64 ELF container differences are not claimed as byte-identical files.

The reused genuine full Core artifact is e9d1c3166e5e16b7949af58d393cabfbfcd2bdb56
fecb1218155b20b6cc00622 from signed production consumer ea1b13a7479e22bb5db908
142197813ce21f940b (PR573), copied byte-for-byte into each owned output. It was
NOT rebuilt at test-only head66bd. Original full Core route was packaged into
both pristine target images BEFORE CMS signing. The ordinary MAIN app was
compiled through the normal native PE linker and relocation audit (actual
84216 exit0 for the three named app/runner preparations); app logs/times and
input manifests preserve each subsequent normal dependency invocation.

Producer and candidates
-----------------------
Actual selected signed producer b06f91d78a891e1d803ea48543bdf545231ef786 is
/home/sean/Documents/.coreboot-worktrees/q35-exclusive-e8-dma-scope-after385 .
Config /home/sean/q35-e8-scope-full.9sM2hH/full.config and its matching cbfstool
remain immutable read-only inputs. Their hashes, target hashes, public signer
hashes, app/Core hashes and candidate hashes are retained in input maps and
fixtures/excluded-fixtures.sha256; large binaries and keys are NOT copied.

Reference /home/sean/system-fmp-provider-session-final.GCQCjf/signed-capsule.bin
is genuine configured-trust CMS attempt0x001a000a/LSV0x001a0009 with INIT_RESET
and the full newer target. All full-width versions are preserved; '9/a/8' are
only suffix shorthand. Explicit fixtures in normal-core-ram-refusal-fixtures
.VxQuI9 are: wrong signer valid under its separate HOST fixture public cert
but invalid under the unchanged genuine producer trust; exact final signed
ROM-byte corruption of the valid reference with framing unchanged; and
genuine producer-key-signed attempt/LSV0x001a0008 below floor0x001a0009.
The rogue public certificate is merely an expected HOST preflight input and
does not replace PUBLIC_TRUST. bind-fixtures.py/binding.log and genuine tool
validation receipts preserve those facts. Local HOST-only rogue private
key/combined signer/P12 files are deliberately excluded, as are all other
private signing material, firmware images, guest disks, sockets and TPM state.

Firmware media outside actual FMAP SMMSTORE (offset0,length65536) is compared
over the complete remainder of the selected 8MiB image against the pristine
original e03749ed...c035d; both lengths must be exactly8MiB. Staging and MTC
may legitimately change SMMSTORE; this is NOT a whole-store byte equality
claim. Instead the genuine producer FV/store/record/semantics/default-store/
writer/FTW codecs, UUID and hexstrtobin decode the actual namespace from the
actual extracted producer config, require clean FTW, attrs3, exactly20 data
bytes and canonical all-zero FmpState. Expected state is source constants,
not learned bytes. Each case has actual O0/O2 strict ASAN/UBSAN store outputs.
Disk equality and all input equality are independent postconditions.

HOST and independent receipts
-----------------------------
Final author oracle 22186 exit0: host/oracle-author (original Q0Pdh7), raw
oracle-final-inline.log. All27 modeled HOST cases, three actual capsule-tool
preflight positives, three valid-reference-as-refusal negatives and seven
exact Python guard-discard causes passed. Every discarded guard must yield
the selected unittest 'ValueError not raised' failure, exit1, not a syntax or
import error; complete inverse-TU comparisons and all13 input checks pass.
HOST tests are mechanism/oracle coverage, not additional native boots.

Final author actual-codec store 50187 exit0: host/store-author (VsjwAY) and
store-author-outer.log. O0/O2 strict ASAN/UBSAN positives and self-tests reject
20 data-byte, one attributes, 16 namespace-byte and 32 FTW-header changes per
optimization. Four exact C guards each have a positive run and targeted
assertion abort134 at O0 and O2, with full inverse-TU equality. These C causal
binaries ARE sanitizer-instrumented; the expected assertion is required and
any sanitizer diagnostic is forbidden. This is not an unsanitized-causal
claim. SAN aliases close actual coreboot HOST integer types only, not policy.

Non-author reviewer actual67402 exit0 independently ran the final oracle
(host/oracle-peer u1TG9E); its final raw HOST count is27, despite an earlier
message counting26. Actual97848 exit0 independently ran the genuine store
script (host/store-peer bkMaXu), strict SAN and all targeted causes. Both
raw outer logs and actual before-input manifests are retained. Their elapsed
times were not separately measured and are not invented.

Reviewer saved-only replay of all three outcomes is copied in
host/native-saved-peer: original Twadtg replay.py/log/time, actual exit0,
GNU0.17 s. It invokes the real frozen runner checks and reads existing saved
media/candidates only; it starts NO VM. Root also independently inspected all
three result/status/maps, retained candidate and complete outside-store bytes.
Root and the non-author reviewer accepted all eight source paths before native
execution, including the exact8MiB/truncation guard and inline unchanged
positive outcome (no single-use wrapper). Historical positive/restore default
source behavior remains intact; no new positive/restore VM is claimed here.

Historical harness failures
---------------------------
history/build-o0.log preserves first store compile1 due missing actual authvar
constant header, repaired by the real include with no authority defines.
history/retry.log preserves the first test-only namespace-mutant positive1
because it selected an earlier unrelated variable; final mutant selects the
actual FmpState name while discarding only its namespace comparison.
history/oracle-final.log preserves initial causal import1 missing the genuine
QMP module path; final runner import uses the actual util module. No failed
before-manifest or causal was relabeled a pass after source changed. Subsequent
author scripts and non-author fresh scripts ran the final frozen bodies.
Packaging's first attempt failed copying a misnamed binding log, before source
archive creation; corrected exact binding.log was copied. PR body edit's old
gh GraphQL Projects error and first empty JSON400 were tooling errors; REST
PATCH0 corrected 'consumes' to 'processes', avoiding a retirement claim.
An initial staging-only check-receipts.py run used guessed marker names and
failed assertion1; its tool output is transcribed in history, not represented
as an original redirected raw log. The final script
uses the exact frozen source marker names, without changing any native input
or oracle. That packet-script failure is not a native or firmware regression.

Integrity and replay limits
---------------------------
source archives are finite selected signed source, not build trees:
consumer-66bd contains all eight changed files plus unchanged real app C/ld
and QMP observer; producer-codecs-b06 contains the genuine nine codec TUs,
their owned/common headers and actual capsule tooling. Corresponding signed
Git blob TSVs/commit signatures bind every archived regular source. Source
archives are snapshots from signed Git; they are not invented before-build
manifests. Original before-input maps separately bind execution inputs.

files.sha256 covers the finite packet. check-receipts.py checks copied input
map equality, result/reset/status/marker cardinality, declared profile and
signed archive blob hashes; it starts no VM and does not manufacture omitted
media/signature evidence. Native retained bytes, firmware, disk and app/Core
binaries are intentionally omitted; excluded hashes and original paths permit
the saved peer replay on this host while those private artifacts remain.

All packet facts concern these actual three native refusals and bounded HOST
checks. Request consumption/retirement, a successful later boot after refusal
and precise authenticated pre-mutation failure classification remain separate
unimplemented lifecycle work. Generic DEVICE_ERROR must not be treated as a
safe recovery receipt.
