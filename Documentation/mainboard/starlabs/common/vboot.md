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

Converting an existing non-vboot installation is not supported by this PR.
