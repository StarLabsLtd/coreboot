# StarBook Mk V

## Specs

- CPU (full processor specs available at https://amd.com)
    - Ryzen 7 5800U (Cezanne)
- EC
    - ITE IT5570E
    - Backlit keyboard, with standard PS/2 keycodes and SCI hotkeys
    - Battery
    - Charger, using AC adapter or USB-C PD
    - Suspend / resume
- GPU
    - Radeon™ Graphics
    - eDP 14-inch 1920x1080 LCD
    - HDMI video
    - USB-C DisplayPort video
- Memory
    - 2 x DDR4 SODIMM
- Networking
    - AX210 2230 WiFi / Bluetooth
- Sound
    - Conexant CX20632
    - Internal speakers
    - Internal microphone
    - Combined headphone / microphone 3.5-mm jack
    - HDMI audio
    - USB-C DisplayPort audio
- Storage
    - M.2 PCIe SSD
    - RTS5129 MicroSD card reader
- USB
    - 1920x1080 CCD camera
    - USB 3.1 Gen 2 Type-C (left)
    - USB 3.1 Gen 2 Type-A (left)
    - USB 3.1 Gen 1 Type-A (right)
    - USB 2.0 Type-A (right)

## Building coreboot

It is not possible to build this board without access to NDA restricted files.

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
| External flashing   | yes        |
+---------------------+------------+

Please see [here](../common/flashing.md) for instructions on how to flash with fwupd.
