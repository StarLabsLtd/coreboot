# Native signed-byte CHECK denial

This additive receipt preserves the correctly version-bound corrupted-input
CHECK test. It does not exercise SET, a completed firmware update, restore,
host power loss, or physical hardware. The actual VM uses copied QEMU secure
pflash and the genuine protected provider. The native reply exposes a generic
typed execution refusal, not a separately decoded CMS failure reason.

The exact six source bodies are archived from signed CDK2
`51e9a0ae8113653827a5f51a848afb1ceb49b821` (parent `ddc800e600ae48da2a6e4d91beec4366b65bf718`).
The followup binds the command's attempted version to the actual parsed signed
capsule version before running the VM. Its guest fixture additionally requires
the attempted version to exceed the real running version and meet the real
floor before emitting the prior-state/CHECK markers. These are test-input
constraints, not new production admission policy.

Both accepted invocations use `MM_APPLY_VERSION=0x001a000a` and
`MM_PRIOR_VERSION=0`, the original pristine baseline, the signed newer-a
capsule/authenticated bytes, and the configured public trust certificate.
`original-input-and-elf.sha256` identifies these original external inputs,
the actual producer configuration/tool, and the two resulting guest ELFs.
Those hashes were collected after the runs; they are not invented pre-run
fingerprints. ROMs, capsules, guest ELFs containing embedded images, disks,
and private signing keys are deliberately not included.

The actual negative VM uses immutable
`q35-authenticated-capsule-fixtures.FtKMJ2/original-pristine.rom`, derived from
the provider-383 initial-version source lineage (provider `6b62fd5d7195e7a2313d286b4ac2d08298286cca`
plus auth `2514b790675965b88c502d387b44464007f554ad`; the later `c7e8aaf6c917c55467e4f4f272aea048c23d518f`
leaf wraps only the HOST test script). Its matching resolved producer input
is `q35-auth-provider-newtrust-baseline.8ELcTU/full.config` and its matching
cbfstool is from that same build. This is not a fresh provider-384 producer
or a native test of the fresh-root-inventory change. The directory's
`after384` label records chronology only. The external input hashes bind
the actual original pristine image, baseline configuration, and tool.

The HOST prerequisite verifies the original CMS under the configured public
certificate, proves that changing only the last signed ROM byte leaves the
capsule framing unchanged, and requires exact PKCS#7 verification failure for
the changed bytes. The guest independently imports the real endpoint, observes
the actual zero initial FmpState and INFO baseline, changes only that last
authenticated-image byte in its owned execution copy, and submits a real CHECK.
It requires a triggered, authenticated typed execution-failed response with
the correct attempted version and unsuccessful status. Entire mapped media
and the exact 20-byte state remain unchanged. Actual CLOSE must succeed before
the terminal success marker; no SET branch is compiled in this variant.
The runner also compares the complete extracted COREBOOT and SECURE_PROBE
regions before and after the VM and requires the genuine debug-exit status.

## Accepted actual runs

- Author session `59804` actually reaped zero. Original output:
  `/home/sean/q35-check-denial-version-bound.iiiddV`, run `run.kfmKnI`.
  Copied `author/native.log` includes GNU time: 17.64 seconds elapsed,
  16.05 user, 1.55 system, peak 150308 KiB. It is the final signed 51e9a0 source.
- Independent session `43744` actually reaped zero. Original output:
  `/home/sean/q35-check-denial-peer.LuINAj`, run `run.MBIWIc`.
  Its elapsed time was not measured. The six relative entries in
  `peer/source-before.sha256` were captured before this final invocation and
  all checked unchanged at its successful completion. Those entries are
  relative to the source worktree, not this evidence directory. Exact current
  source versus signed 51e9a0 comparison also returned zero.

Both raw logs report the HOST CMS/framing proof and the actual native
corrupted CHECK refusal, unchanged media/state, and CLOSE. Copied serial logs
retain the actual endpoint, RAM-policy, prior-state, and final denial markers.
Resolved configuration/header copies are after-run snapshots, not caller
before/after identity claims. The public certificate is public-only.

## Invalid earlier invocations retained

`qualifications/initial-nonnumeric-version.log` preserves an initial literal
`a` invocation, rejected by the numeric validator with Make exit 2 before
compilation or a VM. `wrong-low-version-native.log` preserves session `25785`,
which returned zero with `0xa` rather than signed `0x001a000a`. That run could
refuse the rollback floor before CMS and is **not** accepted corrupted-signature
evidence. The contemporaneous qualification text is retained unchanged.

The mandatory version-binding followup rejects `0xa` before CMS or a VM;
`host-wrong-version-binding.log` preserves that exact refusal. The corrected
full-version runs above supersede the invalid invocation as proof, without
rewriting or dropping it. Earlier author 73398 used the correct full version
but preceded this mandatory binding; it is not substituted for final 59804.

Positive CHECK subsequently passed in an incomplete SET attempt, outside this
negative packet. Completed SET, independent cold installed-image validation,
and restore remain open. This is not an all-project or whole-suite completion
claim.
