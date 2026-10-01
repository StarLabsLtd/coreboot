# PR465 selected-profile replay

Frozen, clean, signed CDK2 source:
`55bd35d644e7b53feedbfa7fdd34e47010dbaf92` (ready PR465).
Independent review verified the original author-to-root patch parity for the
separate PR462–464 changes; PR465's test-only capsule oracle was also reviewed.

Root independently reaped these actual selected-profile commands with exit 0:

- FAT, English and checkpatch-filter tests: 18.565s. The stack limit was 64 MiB;
  the resolved Q35 setup profile and alternate build directory were explicit.
- Capsule runtime and disk tests: 3.179s, including the selected direct-call
  relocation assertion. These are focused host tests, not real flash updates.
- Native image and LVGL renderer tests: 1m26.080s; direct composition 36 records/
  eight mutations, ELF layout, coreboot/MTRR tests, LVGL provenance, form and
  framebuffer/status-ownership tests all passed.
- Actual ROM pairing with coreboot PR349 `4dd7974f5a5ec4a6e849cb76f0f81d1212645148`:
  8.713s. Its pairing manifest identifies this exact CDK2 revision.

The selected config/header SHA256 values are respectively
`b01cc23d5abc7a07b1daae14cb758a381b4c6a4ec39d9551619eb3cb5d14eaeb` and
`4236bb2180e7fb61dee32ba9dc2e84458929d407b42f83a4e6d25c5c5486b292`.
Payload ELF SHA256 is
`552df0fde1ae6b24034fe36099caee03dac7b30b0776756b8ac7b970e61c16e1`;
ROM SHA256 is
`f3118dda1eb1560ca009b0a9b1536b293e992c9ffe087a95ebdee0c407990206`.
Both are byte-identical to the separately archived PR459 firmware, and actual
`cmp` of the ROMs returned 0. The retained PR459 archive contains those binary
bytes and four actual QEMU boot receipts. This is binary parity, not a claim
that four additional guests were started at PR465.

The initial FAT invocation returned 2 because root supplied a nonexistent
profile path. The initial pairing invocation returned 2 because root omitted
the setup-profile selector and the default-profile assertion rejected setup.
Both failed receipts are preserved separately; corrected retries passed.
These command-input failures are not firmware regressions.

The later whole-suite run is recorded separately after its actual completion;
these focused results do not substitute for it. This remains a DEBUG
compatibility-provider profile, not GENERAL protected-policy activation,
native interrupt-window execution, hardware validation, full-tree style
acceptance or final project sign-off.
