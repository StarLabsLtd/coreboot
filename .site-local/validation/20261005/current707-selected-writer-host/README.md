# Current 707 selected-boot writer: finite HOST evidence

Source: signed CDK2 PR707 commit `c1dba725d3734bb1f737c6ec08b4cec68e16967e`
(signature status G). This packet records HOST models and source checks only.
Real selected-boot guest acceptance remains OPEN. It contains no UKI build,
firmware build, TPM guest, live PCR, cold-media, or hardware gate.

## Literal receipts and results

The twelve initial final log/status members and `cc-o0` are literal copies from
`/home/sean/current693-fwui-qemu.PMINOC/selected-writer-root-host.N59JCy4W`.
The later verified O0 pair was copied from that same directory after completion.
Every retained final status is raw zero.

- `writer-hostcc-o0-verified.{log,status}`: 21 HOST tests pass. The actual rerun
  used `env HOSTCC=/home/sean/current693-fwui-qemu.PMINOC/selected-writer-root-host.N59JCy4W/cc-o0 python3 -B tests/linux_fwui_request_test.py`.
  Both test compiler calls consult HOSTCC. The literal wrapper executes
  `/usr/bin/cc "$@" -O0`, so its final optimization flag overrides the fixture's
  hardcoded `-O2` without removing ASan/UBSan, no-recovery, or no-PIE flags.
- `writer-o2-final.{log,status}`: 21 HOST tests pass using the fixture's ordinary
  O2 compiler arguments, including ASan/UBSan and no-recovery/no-PIE. The models
  exercise the real requester against regular files, not efivarfs or firmware.
- `writer-o0-final.{log,status}`: earlier 21-test pass retained unchanged. Its
  optimization invocation was not established; the reported CC override would
  not select a compiler in this HOSTCC-based fixture. This pair is NOT accepted
  as O0 evidence. Only the verified HOSTCC rerun above supplies that gate.
- `oracle.{log,status}`: event-log oracle selftest passes. Selected PCR1/4/5 and
  four-bank replay/wire/digest oppositions are constructed HOST cases, not live
  TPM measurements. Ordinary fallback behavior remains a separate strict mode.
- `stable-lint.{log,status}`: all sixteen stable lint stages pass. The retained
  output includes binary-file grep notices; it is not an empty log.
- `shell-syntax.{log,status}`: shell syntax check succeeds with an empty log.
- `owned-c-style-final.{log,status}`: raw zero, zero errors and zero warnings,
  but fifteen CHECK diagnostics remain. Fourteen are kernel-width suggestions
  on this native userspace stdint code; one is the unchanged pre-existing FWUI
  continuation alignment at line 261. The three new alignment groups were
  corrected before this final check. This is not a zero-diagnostic style claim.

The test source SHA256 is
`088db1ec9a8b8856d2802df9208f03ecbdf505833dfb87b39330a0a14a8a05ac`;
the requester source SHA256 is
`d6f4d5d35a1c081c782b08321cbfd4b6734de84f5a0b3b25157bcc850840132d`.
These two bodies remained identical to PR707 during the verified O0 rerun;
the unrelated boot-logo successor edit was not an input to this test.

## Preserved author failure

`author-fault-anchor-failure.{log,status}` is a literal renamed copy of
`/home/sean/current693-fwui-qemu.PMINOC/selected-writer-author-host.scRgRbFv/request.{log,status}`.
Its raw status is one: the then-17-test suite reported five failures in the
readback-fault opposition's brittle read-call-number anchors. This was not a
production runtime failure or a sanitizer finding. The final test injects the
readback mutation after the actual selected write ordinal instead. The failure
has not been rewritten or counted as a successful gate.

## Limits and open acceptance

The writer's fixed source-defined A/B/C options, repeated BootOrder A/B/A and
outside-order BootNext C are HOST-tested for legitimate write/readback behavior,
attributes 7, refusal of pre-existing metadata, and error propagation. The
verify-only model requires BootCurrent C with attributes 6 and absent BootNext.
The source-bound expected JSON and canonical GPT route do not prove real
SetVariable persistence or actual BDS image selection.

Acceptance still requires a genuine protected TPM producer profile, current
source-owned UKI/manifest, pristine first guest, successful real writes, literal
first-after flash binding for a cold second guest, selected image/path evidence,
and live PCR/event-log replay. A failed final BootNext write/readback must never
be treated as a completed first guest or an admitted cold seed. TPM-disabled
normal UI receipts are not evidence for these remaining gates.

`SHA256SUMS` lists the exact eighteen other packet members, including this README.
The packet has nineteen members total; no transient test binaries or broader
home-directory inventory is included.
