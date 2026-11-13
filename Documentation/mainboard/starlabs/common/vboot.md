# Star Labs verified boot

Verified boot is opt-in for HZ, B8 and F2. Release configuration is maintained
outside this repository because it contains board blobs and signing inputs.

| System | Firmware GUID |
| --- | --- |
| StarBook Horizon (HZ) | `b96a6b09-dc2c-4e3a-86ee-47016c7b035a` |
| StarBook Mk VIII (B8) | `581c8874-50e1-4231-af48-04cff1c4668f` |
| StarFighter Mk II (F2) | `eef5c7c3-124f-406a-87fe-4b58589fd331` |

The release configuration selects the board's `vboot.fmd`, TPM-backed anti-rollback,
primary-flash VBNV, measured boot, controller protection, the post-lockdown
VBNV completion callbacks and the routine capsule region set:

```
EC RW_NVRAM FW_MAIN_A RW_FWID_A VBLOCK_A
```

Before a release build, supply the approved 40-character EDK2 commit through
`CONFIG_EDK2_TAG_OR_REV`, the matching release `CONFIG_LOCALVERSION` and
signing/keyblock version, the board's FSP, descriptor, ME and EC blobs, and
the signing-key directory below. This tree contains no approved EDK2 commit
or matching signing-version input, so it cannot itself produce a release
image. The release policy rejects a moving EDK2 reference,
mocked secdata, disabled rollback protection and unsafe developer GBB flags.

The release checkout check accepts a dirty EDK2 tree only when its sole tracked
change is the configured generated `MdeModulePkg/Logo/Logo.bmp`; every other
tracked source change or submodule revision mismatch is rejected. The logo is
an explicit build input, rather than an exception for arbitrary local edits.

The intended image has one signed read-write slot and an immutable recovery
image. Intel Fast SPI applies and locks a controller protected range for
`WP_RO`.

F2 and B8 have been hardware validated with this controller-protection flow.
B8's Fudan status register 2 is provisioned and verified with an external
programmer. That provisioning evidence is distinct from the runtime controller
range readback: Fast SPI does not read status register 2, and coreboot does not
treat configuration as a hardware readback. HZ selects the same controller
protection policy, but this does not extend the F2/B8 validation to HZ.

B7-U also has layout/policy plumbing. Its release configuration remains
non-vboot with stock GUID `85584837-7b03-4b6c-940c-8b6186cbf7a1` and capsule
regions `COREBOOT EC`. New vboot identity, rollback/signing version and migration
contract remain pending; do not publish a conversion under these settings.
Its layout retains the 32 MiB flash, BIOS base `0x01200000` and 14 MiB BIOS
span, with an 8 MiB top-aligned `WP_RO`. HAS_WPS describes a capability, not
provisioned-clear status. No B7-U WPS_PROVISIONED_CLEAR assertion is made.

Routine capsules update only the EC, flash-backed VBNV and signed read-write
tuple. The capsule check requires an erased `RW_NVRAM` image, which clears the
previous image's successful-boot result. A newer RW image cannot advance TPM
rollback state until it has reached ramstage and recorded its own successful
result.

The release policy requires TPM2-backed vboot secdata, primary flash VBNV and
measured boot. It rejects mocked secdata, disabled firmware rollback checks
and the GBB flags
that force developer boot.

## Capsule reset requirement

The reset used to hand a capsule back to coreboot must reset the SPI vendor
capability lock. These release configurations use the Star Labs EDK2 payload,
whose warm-reset path consumes the FADT reset register. coreboot publishes the
CF9 system reset (`0x06`) there when `HAVE_CF9_RESET` is enabled, as it is for
all three systems.

Do not substitute a payload or capsule flow that performs only a CPU reset. The
reset must clear VCL so coreboot can program and lock the controller protected
range before launching the payload.

## Signing keys

The F2 release configuration must provide its established firmware GUID,
matching release `CONFIG_LOCALVERSION` and keyblock version, and the following
provisioned signing inputs. The current keyblock input is version 6665 for
release 26.09; this branch intentionally rejects a later local version until
the matching signing input is supplied.

- `root_key.vbpubk`
- `recovery_key.vbpubk`
- `firmware_data_key.vbprivk`
- `firmware.keyblock`
- `kernel_subkey.vbpubk`

The build rejects the in-tree vboot development keys by default. For a
development image, explicitly set `STARLABS_VBOOT_ALLOW_TEST_KEYS=1`; never use
that override for a release image.

Set `STARLABS_VBOOT_KEY_DIR` to the directory holding these files before a
release build.

## Existing installations

An F2 A/B conversion is a separate, explicit capsule-build operation. Set
`CAPSULE_LAYOUT_TRANSITION=1` and `CAPSULE_GUID` to the installed normal
firmware's identity. The capsule signature binds that installed identity; the
new payload retains its distinct vboot identity. The builder verifies the
complete new image, its FMAP and its resident FMP driver. This operation replaces
the BIOS layout and requires the installed firmware's authenticated full-BIOS
update route. It is not an ordinary vboot update and does not provision permanent
chip write protection. Existing firmware compatibility and recovery must be
validated before conversion.

Read and validate the existing TPM firmware rollback index before selecting the
conversion version. Both signed slots must be eligible under that stored floor;
a valid capsule signature does not override vboot rollback checks. Do not clear
the TPM or disable rollback checks to make an older candidate boot. If the
firmware index is absent, the upstream factory-initialization path clears the
TPM before provisioning it. Customer conversion therefore needs an explicit
TPM-provisioning policy that accounts for existing TPM-backed credentials.

## F2 two-slot profile

`VBOOT_SLOTS_RW_AB` enables an experimental F2 layout with two equal RW slots
and a 4 MiB immutable recovery region. The existing A-only layout and other
normal builds are unchanged. This profile requires the matching private
capsule-handoff revision in the IA32 MM owner and the X64 payload.

Routine capsules name exactly these regions:

```
FW_MAIN_A RW_FWID_A VBLOCK_A FW_MAIN_B RW_FWID_B VBLOCK_B
```

The protected owner selects only the inactive slot from the firmware-owned
handoff. It rejects layout fallback and programs neither RO nor the running
slot, EC, variable store or VBNV through the image's update ranges. Once every
block has been verified and the selected ranges programmed successfully, a
private callback arms three attempts of the completed slot. Cancellation and
incomplete programming do not arm it.

A separate authenticated EC-only capsule remains supported. It updates only the
matching EC region and does not arm an A/B trial or change its NV state. Mixed EC
and firmware manifests are rejected by the A/B updater.

The A/B profile uses two erase-aligned VBNV banks. Records are appended and
read back before their final commit word is programmed. Rollover erases the
other bank, preserving the most recent committed record. The sequence number
orders records; it is not an anti-rollback counter. This new format is part of
the explicit layout conversion, not an in-place migration of an A-only store.

Native ramstage does not acknowledge A/B success. The existing protected MM
ExitBootServices notification checks the executing RW slot against VBNV before
recording success. It clears that slot's remaining trial count, but preserves
an update armed for the other slot. Recovery never acknowledges RW success.
An acknowledgement failure is logged; the firmware does not invent a successful
write. S3 reuses the completed lifecycle and does not acknowledge again.

This boundary represents firmware-to-loader handoff, not successful OS boot.
The notification can be sent by privileged DXE code, and vboot may advance its
TPM firmware-version floor on a subsequent boot after success. An older fallback
can then become ineligible. These consequences must be considered before
provisioning rollback policy.

The vboot payload defaults Secure Boot on only when enrolling the initial keys.
It preserves existing enrolled keys and the user's existing enable setting.
SMM BIOS write protection is required and its runtime override is unavailable.
Neither these settings nor a successful build prove permanent flash protection
or hardware A/B recovery.

## Strict measurement profile

The strict measured profile records bounded, versioned ASCII policy events in
`PCR_RUNTIME_DATA` (PCR 3). The digest covers ASCII bytes excluding the terminating
NUL; the event log retains the same text. TME records distinguish the requested
setting, CPU capability and final FSP-M input after board/debug overrides. A later
BSP snapshot records TME_ACTIVATE independently. The CSE event separates requested
policy from observed working state/operation mode, including unavailable reads.
BIOS protection records configuration and actual readback after SMM finalization.
CPU identity and observed microcode revision describe the post-initialization
state. S3 does not emit a fresh set of policy measurements.

The separate RO FIT table is explicitly mapped for measurement. The ordinary
RO microcode blob is also measured, including when FIT could have consumed it
before its first software mapping. Split microcode-bin coverage is not established
by this addition. Neither a table hash nor a later revision observation proves
what executed before the CRTM started. ME state is not executable ME attestation,
and an EC flash-image hash does not identify the running EC firmware.

The vboot producer requires successful TPM2 SHA256 measurements in EDK2. Its
native-log importer checks Spec ID framing, unique declared algorithms, SHA256,
per-event bounds, addresses and HOB allocation. Unused zero capacity is accepted;
a malformed or incomplete strict import stops before DXE. Resident-FV bootstrap
retains the existing classification so TCG2 can load; external images require the
TCG2 SHA256 service, and the platform checks readiness before capsules or device
connection. This reuses the existing FV trust model rather than creating a new
cryptographic origin guarantee. Failed PCR7 policy measurements cannot silently
continue in this profile. The fixed ACPI event log is reserved before publication
for the existing main log's maximum growth, plus truncation-marker headroom.
At the current defaults this retains about 1 MiB of ACPI NVS. Exhausting either
required log stops the strict handoff; it must not silently publish an incomplete
prefix. Default-off builds keep their existing allocation and permissive policy.
