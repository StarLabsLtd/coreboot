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

The release configuration uses the AMD IA32 PEI graphics driver through
coreboot's PI host after openSIL and PCI resource assignment. Supply the
board-compatible graphics inputs separately:

```
CONFIG_RUN_AMD_PEI_GOP=y
CONFIG_DRIVERS_AMD_PEI_GOP_FILE="/path/to/AmdIa32GenericGop.efi"
CONFIG_DRIVERS_AMD_PEI_GOP_ATOM_FILE="/path/to/amd_vbios.rom"
```

Coreboot passes the initialized framebuffer to generic EDK2 and publishes
the updated ATOM image through its existing VFCT writer. The payload does
not need the AMD DXE GOP or its platform glue. These binaries remain subject
to their applicable distribution terms.

The configuration enables capsule updates and signed capsule generation.
Supply signing certificates at the configured paths before building; private
key material is not included in the repository.
