# CDK2 Selecting-status QEMU evidence

This is a source-bound evidence staging directory for PR #452, commit
`6d760cf2f98f25881737c54e9dd95d48200f6295`, directly based on
`16520afbee6ee5d4b9ff0e23f777e9c641c893b8`. The captured exact-head QEMU
runner exited 0. It reports the real setup hotkey acceptance and the
BootToFwUI request/presentation two-boot acceptance passing.

## Scope

This proves the bounded Q35/QEMU setup flow and the test harness's status-strip
oracle on the existing debug-status profile. The `Selecting boot device`
pixels are compared with a native raster reference rendered from the pinned
LVGL source and exact selected configuration, then sampled again after a
250-ms no-input interval before F2. It is not a hardware, release-profile,
production image-hash execution, or protected-variable activation claim.

The archive contains raw PPM screen captures, QMP/serial/debugcon and QEMU
logs, manifests, relevant pflash before/after images, exact ROM, prepared
native config/header/binary, the source patch, and fixture inputs. It omits
per-boot duplicate `nvme.raw` and `usb.raw` copies because manifests record
their input hashes and the single source fixtures are included under
`inputs/`. Runtime TPM state directories, QEMU pid/socket files, and private
keys are excluded. The pflash files are retained because they are the measured
boot-variable state for the request/presentation pair; they contain no test
private keys.

## Identity

- Source commit: `6d760cf2f98f25881737c54e9dd95d48200f6295`
- Parent: `16520afbee6ee5d4b9ff0e23f777e9c641c893b8`
- PR: https://github.com/StarLabsLtd/cdk2/pull/452
- ROM SHA-256: `7f7b605b3e2c3f40b44cd1db5392becb5ca6d319715f0d341c65ba61e2990b46`
- Raster reference SHA-256: `0f61c6d78d59b7e222a6e9820fdd45613cf90186946aec156922bcb6dfdf6e33`
- Pinned LVGL commit: `85aa60d18b3d5e5588d7b247abf90198f07c8a63`
- Selected config SHA-256: `b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb`
- Selected `config.h` SHA-256: `4236bb2180e7fb61dee32ba9dc2e84458929d407b42f83a4e6d25c5c5486b292`
- Native UI test SHA-256: `46ec51329f9da1d034c206212366f15612990874df28f0be5581d894a7a42f76`

The manifests preserve original runner paths and input hashes. Raw source
inputs and configuration copies are separately retained here for portable
inspection. No manifest fields were rewritten to make the tests pass.

## Receipts

- `cdk2-selecting-status-final6d760-authoritative-pass.log`: exact-clean-head
  three-run bundle; authoritative shell exit 0.
- `cdk2-selecting-status-final6d76-canonical-checkpatch-root.log`: independent
  configured checkpatch for the final patch, exit 0.
- `cdk2-selecting-default-scale-native-opposing.log` and
  `cdk2-selecting-default-scale-oracles-opposing.log`: independent native and
  synthetic hostile-oracle receipts for the source-identical final patch.
- `cdk2-selecting-status-prepared-input-negatives-opposing.log`: provenance
  refusal cases, including wrong/missing/mutated prepared inputs and foreign
  LVGL source.
- `cdk2-selecting-status-marker-oracles-opposing.log`: synthetic oracle suite
  after correction to the actual `NOT_STARTED` serial marker.
- `cdk2-selecting-status-final6d760-lint-stable.log`: `make lint-stable`,
  exit 0.
- `cdk2-selecting-status-final6d760-lint-stable.log`: `make lint-stable`,
  exit 0, with all 16 checks passing.
- `lint-summary.txt`: exact composite-lint exit/failure summary.

The exact composite `make lint` run reaped exit 1 with one failed lint script:
`lint-007-checkpatch`. The full-tree scan reported checkpatch diagnostics
across unchanged baseline files; it is not a successful composite lint
receipt. A separately configured patch check of the PR diff passed. No
per-file or new custom checkpatch suppression was added. The repository's
configured checkpatch implementation has its existing ignored message types;
the canonical patch-check receipt records that list.
