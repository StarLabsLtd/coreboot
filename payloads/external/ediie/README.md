# edIIe payload

Before configuring edIIe, initialize the recorded payload and dependency
revisions from the coreboot checkout:

```sh
git submodule update --init --checkout --recursive -- payloads/external/ediie/ediie
```

Select `PAYLOAD_EDIIE` on a supported x86 board and enable ASS. For firmware
updates, enable the EFI capsule driver and configure the trusted public
certificate and update regions. edIIe accepts capsules on disk only. Normal
builds verify the payload revision without updating its checkout.
