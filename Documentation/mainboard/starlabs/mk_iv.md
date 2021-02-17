# Star Labs LabTop Mv IV 

## Specs

- CPU (full processor specs available at https://ark.intel.com)
    - Intel i7-10710U (Comet Lake)
    - Intel i3-10110U (Comet Lake)
    - Intel i7-8550u  (Kaby Lake Refresh)
- EC
    - ITE IT8987E
    - Backlit Keyboard, with standard PS/2 keycodes and SCI hotkeys
    - Battery
    - Charger, using AC adapter or USB-C PD
    - Suspend / resume
- GPU
    - Intel UHD Graphics 620
    - GOP driver is recommended, VBT is provided
    - eDP 13-inch 1920x1080 LCD
    - HDMI video
    - USB-C DisplayPort video
- Memory[^1]
    - 16GB on-board for Comet Lake platforms
    - 8GB on-board for Kaby Lake Refresh platform.
- Networking
    - AX201 CNVi WiFi / Bluetooth soldered to PCBA (Comet Lake)
    - 8265 PCIe WiFi / Bluetooth soldered to PCBA (Kaby Lake Refresh)
- Sound
    - Realtek ALC256
    - Internal speakers
    - Internal microphone
    - Combined headphone / microphone 3.5-mm jack
    - HDMI audio
    - USB-C DisplayPort audio
- Storage
    - M.2 PCIe SSD
    - RTS5129 MicroSD card reader
- USB
    - 1280x720 CCD camera
    - USB 3.1 Gen 2 Type-C (left)
    - USB 3.1 Gen 2 Type-A (left)
    - USB 3.1 Gen 1 Type-A (right)

[^1] The memory is based on hardware configuration resistors see `src/mainboard/starlabs/labtop_mk_iv/romstage.c`

## Building coreboot

### Preliminaries

Prior to building coreboot the following files will need to be provided:

- Intel Flash Descriptor file (descriptor.bin)
- ITE IT8987E firmware (it8987.bin)
- Intel Management Engine firmware (me.bin)
- Splash screen image in Windows 3.1 BMP format (Logo.bmp)

Default directory for these files is `3rdparty/blobs/mainboard/starlabs/labtop_mk_iv`

### Build

The following commands will build a working image for a Comet Lake based PCBA:

```bash
make distclean
make defconfig KBUILD_DEFCONFIG=configs/config.starlabs_labtop_mkiv_cml
make
```

For the Kaby Lake Refresh based boards use:

```bash
make distclean
make defconfig KBUILD_DEFCONFIG=configs/config.starlabs_labtop_mkiv_kblr
make
```

## Flashing coreboot

```eval_rst
+---------------------+------------+
| Type                | Value      |
+=====================+============+
| Socketed flash      | no         |
+---------------------+------------+
| Vendor              | Winbond    |
+---------------------+------------+
| Model               | 25Q128JVSQ |
+---------------------+------------+
| Size                | 16 MiB     |
+---------------------+------------+
| Package             | SOIC-8     |
+---------------------+------------+
| Internal flashing   | yes        |
+---------------------+------------+
| External flashing   | no         |
+---------------------+------------+
```
