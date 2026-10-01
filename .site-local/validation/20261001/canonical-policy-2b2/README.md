# Exact PR459 compatibility validation

CDK2 source is signed `2b2dd79de174154e43add5f589cf07e9d726126e`
(ready PR459), paired with coreboot
`4dd7974f5a5ec4a6e849cb76f0f81d1212645148` (ready PR349).
The ROM SHA256 is
`f3118dda1eb1560ca009b0a9b1536b293e992c9ffe087a95ebdee0c407990206`;
native ELF SHA256 is
`552df0fde1ae6b24034fe36099caee03dac7b30b0776756b8ac7b970e61c16e1`.
The pairing manifest, resolved configuration, native inventory and signed
linear PR455–459 format patches retain their original identities.

Actual root results, all independently reaped:

- Continuing `make -k -j4 check native-stage`: exit0, 13m25.093s. Its reused
  `build/protected-variable-root-whole-e468` configuration is separately
  `8a70dc70dc9cb75849aa1bd9cc15fa472a22a1a9d131a011b38a290130cdba46`,
  not another clean selected-profile build.
- Fresh selected native image and LVGL build: exit0, 1m42.994s; configuration
  `b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb`.
- ROM pairing: exit0, 5.843s.
- Setup runner retry1: exit0, 2m24.825s, with three actual QEMU boots: F2,
  navigation/Escape/Linux continuation; persisted OS firmware-setup request;
  presentation on the following boot without F2.
- Runtime-services linear oracle against `setup-retry1/hotkey/run`: exit0.
- Separate actual guest BGRT table/image probe: exit0, 58.230s, matching the
  StarLabs bitmap SHA256
  `a05b76f687b176e39376a5fe2e1955cf6f5cd36435b9ef20ab66bb28d5f3909a`,
  xoffset283/yoffset183.

Root visually inspected the actual Selecting boot device capture and navigated
LVGL form. The PNG views are lossless conversions of the retained original
PPM captures, not generated UI mockups. These DEBUG-profile results do not
establish release-profile status rendering.

The first setup command returned2 before starting a guest: it set CBFSTOOL
instead of required CDK2_AB_CBFSTOOL. That failed raw receipt is retained; the
separate retry did not overwrite it. The BGRT top-level raw log captures its
oracle output; guest serial/debug/SPI logs and manifest are retained separately.

Regular capture/manifest/flash-state files are included; TPM state, sockets and
PID files are not. Original source fixture inputs are included once under
`inputs/`, not duplicate NVMe/USB clones from each run. Original absolute paths
in manifests remain unchanged. These boots ran alongside other tests, so
their wall-clock times are test durations, not comparative boot benchmarks.

This is the compatibility variable-provider composition. It does not activate
the GENERAL protected consumer or prove native CLI/STI policy execution. Op8
PR460 and later style/entry integration are not part of this firmware image.
No hardware, full-tree style or final-project completion is claimed.
