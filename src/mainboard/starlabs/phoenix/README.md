# StarFighter Phoenix build inputs

The Phoenix EC firmware is not supplied by the blobs repository. A complete
SPI image must include a separately supplied, validated Phoenix EC binary:

```
CONFIG_EC_STARLABS_ADD_ITE_BIN=y
CONFIG_EC_STARLABS_ITE_BIN_PATH="/path/to/validated-phoenix-ec.bin"
```

Set the path to the actual binary before building. Do not disable EC inclusion
to work around a missing file: a full SPI image without EC firmware can leave
the laptop unable to power on. Merlin firmware for another board is not a
substitute for the Phoenix reference EC.

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

The release configuration requires the Phoenix external-GOP and selectable
CF9 reset support in the StarLabs EDK2 payload. Apply those payload changes
before using the configuration until they are included in its selected
branch. The external GOP path also requires the separately supplied
`UefiPayloadPkg/AmdX64GenericGop.efi` and `UefiPayloadPkg/amd_vbios.rom` files
in the payload source directory.

Capsule generation requires locally supplied signing certificates. The
configuration enables capsule updates but does not include private signing
paths or generate a signed capsule by default.
