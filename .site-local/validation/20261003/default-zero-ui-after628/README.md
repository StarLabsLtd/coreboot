# Normal default-zero UI checkpoint (628 firmware)

This finite packet preserves normal protected/SystemFmp Q35 evidence for the
unchanged default `BOOT_TIMEOUT=0`: one no-key control and one real queued-F2
setup/navigation/restoration run. It is not a hardware, BootToFwUI, Selecting
stability, Linux runtime-variable lifecycle or whole-project signoff.

The production source is signed ready628 `d8e49af2ab0c90c6f6542b29026bd80309a9e1da`.
Fresh normal Core was built without object/header seeds (78.48s); its ELF hash is
`6c86f953340891eac7f847508d9a65c6c67f0ac30c56905b5ee824a08282d08d`.
Producer389 `7ee34bed989c46913c3ee6672fb25e83227c3b6c` packed that Core before
the version9 ROM was built (47.58s; ROM hash
`e381f1268fbc56e15a261fc7238206f97229430df4c4752cca192aac9fd697ae`).
The genuine profile is protected1/SystemFmp1/TestFmp0/acceptance0/debug0,
LVGL1/hotkey1/timeout0. No firmware delay, request/NVRAM seed, late grant or
security policy change was used. Build recipes, configs, outputs inventories
and raw before/after input maps are included; executable/ROM/media contents
are intentionally omitted.

## Actual successful native runs

`native/default-zero-no-key-3` used observer f4d823, zero submitted key events,
and genuinely completed ordinary runtime: failure null, observer0/QEMU0,
36.99992313s within the original180s bound (outer GNU37.61s). Typed hotkey wait0
and NOT_READY with no form are the expected no-key result.

`native/default-zero-queued-hotkey-7` used observer c39ff970, not the later
retirement cleanup. Failure null, observer0/QEMU0,39.156988156995794s within
180s (outer GNU40.13s). Its checked pre-BDS paused snapshot precedes a single
same-QMP-connection `cont` plus routed F2-down pipeline; all command IDs/replies
and later key-up are recorded. QMP acknowledgement alone is not HID delivery.
The actual unique typed wait0 then resultSUCCESS0 and form entry establish the
source-defined key branch. The result's0us is clock-converted12 ticks, not an
observed raw scan-code field. Real Down navigation changes help, Esc returns,
OS_HANDOFF begins before ReadyToBoot/logo restoration, and ordinary runtime
completion follows. No Selecting persistence is claimed.

The form/navigation/restored original PPMs, generated PNGs and lossless
conversion recipes/receipts are preserved. Human inspection found readable
Graphical UI Configuration controls, changed centered-aspect-frame help after
Down, and clean STARLABS restoration. Native7 PPMs equal the corresponding
viewed native6 images byte-for-byte; native6 aggregate still failed.

## Preserved failures (not firmware regressions)

Exact original directory names and outer receipts are retained:

- `default-zero-hotkey`: typed ConsoleSnapshotMotion during live INPUT_UI read,
  wall1.88268s; QEMU0. Narrow new-mode wait retry later addressed this.
- `default-zero-hotkey-2` and `default-zero-no-key-2`: terminal PhysicalReadError
  after ordinary guest S5 shutdown (39.0534/37.5915s). `-no-shutdown` retains
  QMP for a fresh strict final capture; no old-snapshot fallback is used.
- `default-zero-queued-hotkey-4`: paused VNC input was dropped by installed
  QEMU; typed NOT_READY and normal runtime, observer deadline180.2746s.
  The separate fixed5s transport probe corroborates paused submission/drop
  versus running delivery, not CDK2/HID admission. No firmware hang claim.
- `default-zero-queued-hotkey-5`: actual form entry/resultSUCCESS0, but observer
  incorrectly expected12us rather than converted0us (wall3.7823s). This failed
  run did not persist completed QMP evidence; no missing replies are invented.
- `default-zero-queued-hotkey-6`: real form/navigation/return and runtime, but
  consuming-window observer incorrectly waited logo before earlier handoff
  (wall180.2768s). Source correction follows actual handoff-before-logo order.

Historical signed observer956/f4/8a/c3d/3f569/c39 sources are archived. Signed
019ca1d retirement removes the known-invalid paused-VNC mode on a separate
branch; it leaves queued/no-key and legitimate delayed/FWUI modes intact.
Original refs, failures and raw evidence are not rewritten.

## HOST scope and verification

Production HOST proof includes twelve P0/P1 × three fixture-variant × O0/O2
strict-sanitized binaries: eight active and four disabled (39.22s), not native
USB hardware proof. The real modeled ConSplitter/USB queue fixture covers528
queued keys; the derived1040 finite budget does not promise continuous-input
fairness. Observer HOST stages and independent receipts preserve exact tests,
source identities and failure qualifiers. Final retirement author35 controller
cases +20 real-reader cases pass; independent35+20 and both saved native
oracle replays pass. These reader tests are modeled, not another native boot.
The initial author HOST run passed17 controller tests but its outer aggregate
was1 because signing changed HEAD/status during the final identity check;
that failure is not relabeled0. Auxiliary failed diagnostic invocations
(including stale moving-HEAD AST checks and wrong-shell syntax checks) retain
their original raw receipts separately; they are not firmware failures or
additional successful gates. This finite packet does not copy every auxiliary
diagnostic from the host's historical working directories.

Run `python3 -B check-receipts.py` after extracting this packet. It verifies
complete finite file closure, original-copy binding metadata, every selected
Git archive blob, successful saved console/table/transcript/config/pixel/time
oracles, and preservation of failed result records. Optional `--originals`
compares the retained host's original receipts; `--git REPO` independently
compares signed Git blobs and signatures. The portable checker does NOT prove
the current state of omitted firmware/media or all1,402 external input files.
Native7's full current-input verification was a separate host-local check.
The cleanup oracle replay is not relabeled as a native run of cleanup source.

No full images, guest disks, private stores, capsules, binaries, objects, keys
or private source contents are copied. Raw CBMEM table/console bytes and raster
images are public evidence only. Root alone integrates reviewed evidence Git.
