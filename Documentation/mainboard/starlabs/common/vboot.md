# Star Labs verified boot

The release configurations for the following systems use verified boot:

| System | Configuration | Firmware GUID |
| --- | --- | --- |
| StarBook Horizon (HZ) | `config.starlabs_adl_horizon` | `b96a6b09-dc2c-4e3a-86ee-47016c7b035a` |
| StarBook Mk VIII (B8) | `config.starlabs_starbook_rpl_u` | `581c8874-50e1-4231-af48-04cff1c4668f` |
| StarFighter Mk I (F1) | `config.starlabs_starfighter_rpl` | `d5971baa-4506-4fbb-b72b-45cd3fe51fd0` |
| StarFighter Mk II (F2) | `config.starlabs_starfighter_mtl` | `eef5c7c3-124f-406a-87fe-4b58589fd331` |

Each image has one signed read-write slot and an immutable recovery image. The
top-aligned `WP_RO` region is covered exactly by the SPI flash block-protect
bits. HZ and B8 support their Fudan FM25W128 parts; F1 and F2 support their
Winbond parts. Intel Fast SPI disables status-register writes and locks that
controller state on every boot after coreboot programs the protected range.

Routine capsules update only the EC, flash-backed VBNV and signed read-write
tuple. Including `RW_NVRAM` clears the previous image's successful-boot result,
so installing a newer RW image cannot advance TPM rollback state until the new
image has reached ramstage and recorded its own successful result.

## Capsule reset requirement

The reset used to hand a capsule back to coreboot must reset the SPI vendor
capability lock. These release configurations use the Star Labs EDK2 payload,
whose warm-reset path consumes the FADT reset register. coreboot publishes the
CF9 system reset (`0x06`) there when `HAVE_CF9_RESET` is enabled, as it is for
all four systems.

Do not substitute a payload or capsule flow that performs only a CPU reset. If
VCL remains set while the flash controller cannot read all status registers,
coreboot cannot prove or reprogram the exact `WP_RO` range and the required
boot-media policy fails closed before launching the payload.

## Signing keys

Set `STARLABS_VBOOT_KEY_DIR` to the directory holding the provisioned signing
inputs before building one of the configurations above. It must contain:

- `root_key.vbpubk`
- `recovery_key.vbpubk`
- `firmware_data_key.vbprivk`
- `firmware.keyblock`
- `kernel_subkey.vbpubk`

The build rejects the in-tree vboot development keys by default. For a
development image, explicitly set `STARLABS_VBOOT_ALLOW_TEST_KEYS=1`; never use
that override for a release image.

For example:

```bash
export STARLABS_VBOOT_KEY_DIR=/path/to/provisioned/keys
make defconfig KBUILD_DEFCONFIG=configs/config.starlabs_starfighter_mtl
make
```

## Converting an installed system

The verified-boot layouts are incompatible with the earlier single-CBFS
layouts, so conversion requires one full-BIOS capsule update. The transition
capsule uses the previous firmware GUID while its image advertises the new
GUID:

| System | Previous firmware GUID |
| --- | --- |
| HZ | `634fefb2-7099-4e5c-8b85-bd458132f51e` |
| B8 | `41627bd8-70a0-490d-b834-5de90ccfb56d` |
| F1 | `ac75492e-18ad-477d-a719-710a0ddb6c2c` |
| F2 | `75b29ff6-e882-41c7-903f-5af89e004113` |

Build the ROM with its new GUID in the board configuration, then override only
the outer capsule GUID with the previous value. For example, the F2 transition
capsule is built with:

```bash
make -B CAPSULE_GUID=75b29ff6-e882-41c7-903f-5af89e004113 capsule
```

Only offer the transition to an installed firmware whose capsule updater can
fall back to updating the complete BIOS region when the current and incoming
FMAP layouts differ. This fallback replaces SMMSTORE and therefore resets EFI
variables. After conversion, publish ordinary capsules under the new GUID;
they must contain only the routine update regions from the board configuration.
