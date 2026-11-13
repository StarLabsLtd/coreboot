# DAVE verified-boot integration

DAVE requires a signed, verified normal firmware boot before coreboot opens its
TPM authentication phase. `DAVE_MODE` requires TPM2 measured boot, SHA256, a TPM2
event log, SMM BIOS write protection, the fixed authenticated SMM variable and
capsule owner (`ASS`) and a board with the protected SRK pin backend. StarFighter
MTL is currently the only board declaring that capability.
The payload must be the approved, pinned DAVE revision, built with the matching
coreboot SMMSTORE protocol and phase contract.

## Payload build contract

DAVE requires the custom EDK2 repository selection through ASS and defaults its
URL to the private `StarLabsLtd/dave` repository. The paired default revision is
`6f8639b24584c2bc3df4dca1ec2bb4580ab03976`, based on EDK2 `starlabs` commit
`d4ccf19d44a95a4cb12e9567811795a973ab7c31`. `EDK2_TAG_OR_REV` must identify the
exact reviewed 40-character commit used for the release.
An explicit `EDK2_REPOSITORY` override may name a mirror containing that same
source. A branch name is insufficient. The existing pinned-checkout validation
still rejects tracked source changes and mismatched submodules, apart from the
exact configured generated logo. DAVE additionally rejects non-ignored untracked
files in the source tree and every initialized recursive submodule. Inspection
errors stop the build. These checks establish source identity; a commit hash and
declared option names do not establish that the code has been reviewed or that
the declarations enforce the intended behavior. Release review must cover the
selected source and its dependencies.

Build in a fresh trusted workspace with a trusted toolchain. The source check
uses Git's ignore rules and does not inspect ignored generated inputs, Python
caches or pre-existing BaseTools products. The normal preparation removes the
payload build output and regenerates selected configuration, but this is not a
hermetic build or a cleanup of every ignored input. Do not reuse an untrusted
workspace for a release.

The producer enables `STARLABS_DAVE`, secure-key authentication, static Secure
Boot, TPM2 and PCR 6. It maps `SMMSTORE_SRK_PROVISION` to
`SECURE_KEY_FACTORY_PROVISION_ENABLE`. Both the X64 payload and the fixed IA32 MM
owner receive the DAVE static-key policy and the `COREBOOT_DAVE_VBOOT` and
`COREBOOT_DAVE_SRK_PIN` assertions. Conflicting custom defines or policy PCDs are
rejected. The selected source must explicitly declare the DAVE build contract;
generic EDK2 cannot silently ignore these switches.

These assertions describe the build configuration. The runtime verified-boot
gate below and the live protected SRK-pin response remain authoritative.

## Authentication phase

At `BS_PAYLOAD_LOAD`, DAVE is mapped only from the CBFS selected by vboot for
the signed RW slot. A missing RW payload fails even when an older RO payload
exists. The mapping uses the existing CBFS metadata and content-verification
path. Whole-RW verification relies on the preceding vboot body verification;
CBFS integration retains the authenticated metadata and per-file hash checks.
Alternate payload-location hooks and generic payload preloading are bypassed
for DAVE because they do not establish that provenance. A failed new load also
clears any earlier payload entry. Normal RO fallback for other firmware files
remains available.

At `BS_PAYLOAD_BOOT`, the DAVE producer requires vboot to be enabled with real
secdata and to have selected a read-write CBFS after running verification. The
normal vboot path verifies the signed slot and firmware before returning, and
locks its TPM rollback state. Developer, recovery, no-boot and S3 flags are
rejected independently. An unavailable context or uncertain platform resume
state also stops before the PCR extension and payload entry.

| Boot path | DAVE authentication phase |
| --- | --- |
| Verified normal cold boot or reboot | Open PCR 6 immediately before the payload |
| Signed trusted provisioning image in verified normal mode | Same verification requirements; initial pin creation may be enabled |
| Developer mode, recovery mode or vboot no-boot state | Stop before opening the phase or entering DAVE |
| vboot disabled, incomplete or using mocked secdata | No DAVE phase |
| S3 resume | Retain the previous TPM lifecycle; never reopen PCR 6 |
| S4 hibernation wake | Perform a fresh verified normal boot and DAVE authentication |

S3 normally returns directly to the suspended OS before payload entry. If it
falls through to the DAVE callback, the callback stops rather than opening a new
phase. This does not add a YubiKey prompt to the OS's existing S3 resume path.
On MTL, `platform_is_resuming()` reports S3 specifically; S4 follows the fresh
boot path. Storage state across power transitions remains a separate platform
and drive validation requirement.

The phase marker remains the SHA256 digest of
`Star Labs pre-OS authentication phase open v1`, extended into static PCR 6.
It identifies the permitted authentication interval, not a firmware image hash.
Verified boot establishes which trusted firmware may open that interval. DAVE
must still combine the phase policy with its key authorization and membership
policy, and close the phase before handing control to an external image.

Other users of `TPM_PREBOOT_AUTH_PHASE` retain their existing behavior. These
additional boot restrictions are conditional on `DAVE_MODE`.

### TPM hierarchy ownership

The MTL vboot path locks the firmware rollback NV index with `NV_WriteLock`.
It does not randomize PlatformAuth or disable the platform hierarchy before
the payload. These rollback locks remain in place. DAVE owns the subsequent
platform closure, including disabling TPM clear and replacing the empty
PlatformAuth before external image handoff. No coreboot lock is skipped to
make that closure work. Other platforms that consume PlatformAuth earlier
require a separately designed trusted handoff before they can support DAVE.

## Protected SRK pin

`SMMSTORE_SRK_PIN` is selected for DAVE under vboot. In the MTL layout,
`SRK_PIN_V1` occupies the dedicated 64 KiB block at BIOS offset `0x000c0000`
(absolute flash offset `0x010c0000`). It contains the provisioned TPM SRK Name,
not a storage credential. The pin is preserved across updates and cannot be
replaced, erased or rebound through the SMMSTORE protocol.

Both raw write paths exclude every erase block overlapping the pin or FMAP.
The fixed MM capsule owner skips the pin and requires selected bytes in FMAP's
block to remain unchanged. Any change to those bytes fails its preflight,
before destructive writes. Embedded capsule drivers are disabled while SRK pin
protection is enabled, so they cannot become an alternate flash-writing path.

The pin layout gives FMAP its own 64 KiB erase block. `RO_FRID`, `GBB` and RO
CBFS begin after that block. Without this isolation, protecting FMAP from erase
would also prevent routine programming of unrelated data in the same block.
`SRK_PIN_V1` and the isolated `FMAP` region carry the FMAP `PRESERVE` flag.
The signed RW slot offsets and the overall `WP_RO` boundary are unchanged.

## Provisioning and layout migration

Provision on a physically checked board using a trusted image signed by the
provisioned vboot root. `SMMSTORE_SRK_PROVISION` permits only the first pin write
to a completely erased pin block; it does not bypass verified normal boot or
authorize replacement of an existing or partially written record. The SMM
backend requires BIOS write protection and rejects pin creation on S3 or after
the payload closes the security boundary.

Complete any initial vboot TPM provisioning before enrolling DAVE credentials.
An absent vboot firmware index can trigger the existing factory TPM-clear path;
do not apply that path to an enrolled customer system without an explicit
credential recovery and reprovisioning procedure. After checking the initial
pin and enrollment, install the customer image with
`SMMSTORE_SRK_PROVISION=n`. A missing, malformed or mismatched customer pin must
stop authentication.

Enabling this layout on an existing installation is a trusted flash-layout
migration. The RO field and CBFS offsets differ from older vboot images, and
the old firmware may not exclude the future pin block from writes. Do not
deliver this conversion as an ordinary inactive-slot update or an old-layout
capsule. Install and verify the complete compatible image through the trusted
factory/service process, retaining an already valid pin and required firmware
and credential state. A change of the TPM identity requires a separate,
explicit credential recovery and enrollment operation.

Subsequent routine updates must keep this FMAP layout and the pin block intact.
The update exclusions are software enforcement after the trusted firmware has
started; they do not prevent an external programmer from replacing flash.
Board root-of-trust and write-protection provisioning remain required.

Recovery currently cannot enter DAVE or release storage credentials. A repair
flow that must run while vboot is in recovery needs a separately specified,
restricted recovery payload and policy; it must not reuse DAVE's normal
authentication-phase marker.

## Validation

`tests/lib/dave-payload-test.c` links the production payload loader, CBFS mapping
and metadata traversal with vboot's software SHA256. It checks RW-only payload
admission, missing RW with an available RO payload, corrupted content and
metadata, stale entries, alternate-provider rejection and preserved generic
fallback. It covers both CBFS integration and the whole-body verification build
profile. The fixture supplies the selected RW device; it does not run the
hardware verifier or prove the provisioned root of trust.

`tests/lib/tpm-preboot-auth-test.c` compiles the production phase callback for
normal, provisioning, vboot-disabled and non-DAVE configurations. It checks
the positive verification gate, forbidden modes, S3 and resume errors, and
fatal TPM extension failures. `tests/drivers/smmstore-srk.c` links the production
SMMSTORE implementation to exercise pin creation, torn writes, immutable pins,
raw-write exclusions and the closed security boundary.
Its ASS variants exercise the client's initial raw-read probe and the same pin
commands while the raw mutation route is disabled. coreboot still publishes
the SMMSTORE communication-buffer metadata under ASS, and the payload imports
that HOB for the real pin transport. `tests/lib/vboot-dave-tpm-lock-test.c`
checks the production rollback lock operation and error propagation without
consuming PlatformAuth.

`tests/drivers/efi-capsule-srk.c` includes the production MM capsule callbacks and
programming helper, checking the pin and variable-store exclusions, unchanged
FMAP preflight and independent RO CBFS erase block. Run
`python3 tests/lib/dave-config-test.py -v` to resolve the production Kconfig
dependencies and compile the MTL flash descriptions with the in-tree fmaptool.
It covers non-vboot, A-only and A/B pin layouts, checks erase-block isolation,
verifies that the capsule test's geometry matches the production layout, and
checks the payload and MM owner build flags and source-declaration checks.
It also checks that a failed payload build cannot succeed merely because an
output file exists. Run `python3 tests/lib/dave-source-test.py -v` for source
admission fixtures with real disposable Git repositories and nested submodules,
including inspection failures and non-ignored untracked files.

These tests do not replace hardware checks of flash protection, TPM startup
and S3/S4 transitions, storage relock behavior, or the complete signed-image
provisioning and update flow.
