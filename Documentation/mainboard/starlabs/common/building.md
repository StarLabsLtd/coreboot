# Building coreboot

## Preliminaries

Prior to building coreboot the following files are required:

### StarBook series:
* Intel Flash Descriptor file (descriptor.bin)
* Intel Management Engine firmware (me.bin)
* ITE Embedded Controller firmware (ec.bin)

### StarLite series:
* Intel Flash Descriptor file (descriptor.bin)
* IFWI Image (ifwi.rom)

The files listed below are optional:
- Splash screen image in Windows 3.1 BMP format (Logo.bmp)

These files exist in the correct location in the [StarLabsLtd/blobs](https://github.com/StarLabsLtd/blobs) repo on GitHub which is used in place of the standard 3rdparty/blobs repo.

### Meteor Lake FSP

The StarBook and StarFighter Meteor Lake configurations select FSP 13.0.15.80.
Place the matching vendor binary and headers beside the coreboot checkout:

```text
intel_fsp/mtl/13.0.15.80/Release/Fsp.fd
intel_fsp/mtl/13.0.15.80/Include/
```

The supplied `Fsp.fd` SHA-256 is
`d2512d43828ca03e7b6f23db2b0889a0b6c1f005cceabf5b1d4cdc56a05af0df`.
Its matching `Include.zip` SHA-256 is
`993561831004fced468c182b3779c8a066f84513cfec6e288043f8bed474c329`.
Use that header set with the binary rather than headers from another release.

## Build

The following commands will build a working image, where the last two words represent the
series and processor i.e. `lite_glkr`:

```bash
make distclean
make defconfig KBUILD_DEFCONFIG=configs/config.starlabs_starbook_adl
make
```
