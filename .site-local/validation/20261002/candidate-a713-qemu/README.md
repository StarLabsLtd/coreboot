# Fresh reviewed candidate QEMU receipts

CDK2 source is clean, signed `a7132929fc3218ae4617bef6cb6fa310e9f59a96`
(ready PR477), after integrated whole-check head `97835a23de`.
The actual coreboot donor is clean signed `4dd7974f5a5ec4a6e849cb76f0f81d1212645148`
(PR349). This is the DEBUG setup acceptance profile, using the compatibility
SMMSTORE provider, not protected authenticated-service activation.

Root actually reaped these managed commands:

- Selected `native-coreboot-image native-lvgl-renderer-test native-nvme-test`:
  exit 0, 28.097 seconds. This builds the full payload ELF, not just native-stage.
- Pair construction with explicit `cdk2-q35-setup-acceptance.defconfig`:
  exit 0, 6.108 seconds. Direct composition and inserted CBFS provenance pass.
- Setup acceptance: first attempt exit 2, rejected by the Q35 DMA test device
  policy before guest execution; preserve it as failed. Corrected environment
  excludes SATA/SD/network and retains NVMe/USB. Retry actually reaped exit 0
  in 1m43.264s. It runs three actual boots: F2/navigation/Escape/OS, OS firmware
  setup request, and subsequent setup entry without F2. All suite assertions pass.
  Root visually inspected the navigated LVGL screen; the PNG is a lossless
  ffmpeg conversion of the archived actual guest PPM, not a generated mockup.
- BGRT first attempt: managed exit 1 after the harness deadline (guest reached
  S5 but default `-no-shutdown` kept QEMU alive). This is not a pass. Retry adds
  `CDK2_AB_ALLOW_SHUTDOWN=1`, reaped exit 0 in 46.443s, then actual guest BGRT
  table/image acceptance returned 0. Exact bitmap SHA256 is
  `a05b76f687b176e39376a5fe2e1955cf6f5cd36435b9ef20ab66bb28d5f3909a`,
  164790 bytes, offsets 283/183. Both BGRT manifests and raw guest outputs remain
  distinct. The baseline derived fixture and mini UKI are unchanged inputs.

The archive contains ROM, ELF, resolved config/header, CBFS/pair manifests,
actual guest serial/QEMU logs, setup screenshots/reference rasters, assertion
outputs, disposable guest TPM state and failed-attempt receipts. Disposable
disk images and socket files are excluded; each run manifest retains disk hashes, device
selection and exit status. This is not a complete replay-input archive on its
own: canonical fixtures remain independently identified in prior evidence.
`units.log` preserves exact managed command lines and process statuses.

Archive SHA256:
`f7d15740365fb4314238d846d06197318115049349be2f6af78679ceb17f9826`.

These four fresh guest boots do not prove release/quiet rendering, comparative
boot timing, native protected-service execution, platform DMA coverage, final
style conformance, capsule hardware round trips or hardware sign-off. Full
default/selected whole checks apply to parent `97835a23de`, not retrospectively
to the three NVMe commits. Relevant module, sanitizer, image and guest checks
cover this candidate separately.
