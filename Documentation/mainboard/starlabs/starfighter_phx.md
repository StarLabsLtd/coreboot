# Star Labs StarFighter Mk II (AMD)

The StarFighter Mk II AMD (F2-A) uses an AMD Phoenix SoC, a 32 MiB SPI
flash chip and a Merlin embedded controller. coreboot uses openSIL for
silicon initialization. The board has a discrete SPI TPM, two NVMe slots,
WLAN and two USB4 ports.

## Building

Start with `configs/config.starlabs_starfighter_phx`. The configuration
uses the Phoenix openSIL submodule at the revision pinned by coreboot.
Supply the board firmware files relative to the coreboot source tree.

| Input | Path |
| --- | --- |
| AMD PSP and supporting firmware | `../amd_binaries/PHX/PSP/` |
| Board APCB files | `../amd_binaries/PHX/APCB/` |
| Merlin EC image for F2-A | `../amd_binaries/PHX/EC/merlin-release.bin` |
| AMD IA32 PEI graphics driver | `../amd_binaries/PHX/GOP/AmdIa32GenericGop.efi` |
| Board ATOMBIOS image | `../amd_binaries/PHX/GOP/amd_vbios.rom` |

The PSP file list is in `src/mainboard/starlabs/phoenix/fw.cfg`. The
APCB directory must contain `APCB_FP7.bin`,
`APCB_FP7_DefaultRecovery.bin` and `APCB_FP7_Updatable.bin`. Use inputs
from the same supported F2-A firmware set. The graphics driver executes
with firmware privileges.

coreboot initializes graphics at the display's preferred mode before
starting EDK2. It passes the framebuffer to the payload and publishes
the updated ATOM image for the operating system. Select **None** under
**Graphics initialization** to build without the PEI graphics inputs.

## Updates

The normal signed update capsule covers the `COREBOOT` and `EC` flash
regions. Configure signing certificates for the target firmware's trust
chain before creating a release capsule. Retain a verified full flash
backup for recovery.
