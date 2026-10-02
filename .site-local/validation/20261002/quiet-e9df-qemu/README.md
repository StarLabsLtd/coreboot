# Quiet payload profile: actual Linux and BGRT

Firmware inputs were frozen at reviewed CDK2 `a7132929fc`; the later signed
`e9df2281de` (ready PR478) changes only QEMU tooling and a test defconfig, not
firmware source. The canonical quiet defconfig contains the same options as
the generated input used to build this ELF. The earlier prepared input and
resolved config/header are included rather than relabelled as newly compiled.
Coreboot donor remains clean `4dd7974f5a5ec4a6e849cb76f0f81d1212645148`.

Actual reaped results:

- Quiet native-coreboot-image and native-lvgl-renderer-test: exit 0, 1m34.278s,
  CPU 1m37.808s. DEBUG, serial and SPI sinks are off; diagnostic CBMEM and
  timestamps remain enabled. Full direct-composition and LVGL gates pass.
- Full QEMU harness selftests: initial exit 1 because the unrelated disk-oracle
  copy exhausted `/tmp` tmpfs, not a source assertion. Failed raw output retained.
  Same frozen source rerun with TMPDIR=/home/sean actually reaped exit 0,
  including resolved quiet configuration and debug/serial/SPI/missing-capability
  rejection cases. Independent source review accepted these five tooling paths.
- Pair construction: exit 0, 5.881s, with the explicit quiet acceptance profile.
  Direct payload and extracted CBFS provenance assertions pass.
- Actual quiet guest: exit 0, 44.894s, CPU 48.374s, with allow-shutdown enabled.
  The ordinary Linux runtime probe reaches `CDK2_RUNTIME_OK`, shuts down, and
  the actual guest BGRT table/bitmap assertion returns 0. This is one actual
  boot; Linux-version lines repeated by guest dmesg are not additional boots.
  Bitmap SHA256 remains `a05b76f687b176e39376a5fe2e1955cf6f5cd36435b9ef20ab66bb28d5f3909a`,
  164790 bytes, offsets 283/183.

ELF SHA256: `e6c02a4bacc00c548c46972d416460511adc212f79b36288f1fd593782b9f2c2`.
ROM SHA256: `d3ea4cdbaf0b059721c43ec148c012c0171f349647d8099a8e90dd0805ea781e`.
Archive SHA256: `088ed4ff17a68ca634383f58a46f2daab337da900f33cebd1e0bc670a7810827`.

The archive includes real ROM/ELF, config/header/prepared input, pair/CBFS and
guest manifests, raw output/assertions and disposable guest TPM state. Disk
images and socket files are excluded; this is not a self-contained fixture
replay archive. units.log records managed command lines and actual statuses.

This validates the quiet **payload** profile, not a silent coreboot donor,
quiet-profile UI interaction, protected-service activation, comparative boot
timing or hardware. DEBUG setup interaction is proven separately. Do not infer
hardware performance from these TCG wall times or claim full roadmap completion.
