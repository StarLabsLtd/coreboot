# StarFighter Phoenix build inputs

The release configuration includes the Phoenix Merlin EC firmware from
`3rdparty/blobs/mainboard/starlabs/phoenix/starfighter/ec.bin` by default.
Do not disable EC inclusion
to work around a missing file: a full SPI image without EC firmware can leave
the laptop unable to power on. Merlin firmware for another board is not a
substitute for the Phoenix EC.

The board firmware configuration in `fw.cfg` also requires the vendor Phoenix
PSP files and FP7 APCB files supplied separately from coreboot. Select that
configuration and set the APCB directory to the actual local inputs:

```
CONFIG_AMDFW_CONFIG_FILE="src/mainboard/starlabs/phoenix/fw.cfg"
CONFIG_ADD_APCB_SOURCES=y
CONFIG_APCB_SOURCES_PATH="/path/to/PHX/APCB"
```

`fw.cfg` lists the expected PSP file names and directory. These external
binaries must be available under their applicable distribution terms; the
board support patch does not grant permission to redistribute them.

The release configuration selects StarLabs EDK2 `26.10_amd`, which includes
Phoenix external-GOP and selectable CF9 reset support. The external GOP path
requires the separately supplied
`UefiPayloadPkg/AmdX64GenericGop.efi` and `UefiPayloadPkg/amd_vbios.rom` files
in the payload source directory.

The release configuration generates capsules for AMDFW, COREBOOT and EC
using the StarLabs signing certificates supplied locally at its configured
paths. The certificates and private signing key are not included in coreboot.
