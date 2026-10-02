# Quiet CDK2 consumer: actual native and three-boot QEMU receipts

The isolated root native aggregate was actually reaped 0. Unit
`cdk2-root-quiet-consumer-after539-isolated`, invocation
`e56f65423aa447beae18f84013ba1ed3`, ran for 2m28.230s (CPU 2m34.341s,
peak 204.5M). Its real strict payload compilation/direct provenance, native
LVGL renderer, Core RAM-window positive/causal tests and QEMU configuration
gates pass. Expected causal assertion failures are not positive-test failures.
Both actual caller config/header fingerprints remained unchanged.

The root three-boot unit `cdk2-root-quiet-threeboot-after539`, invocation
`648686ad19b44110ae5612fbd81f3479`, was actually reaped 0 after 1m55.432s
(CPU 2m33.850s, peak 696.7M). It exercises genuine F2 setup entry, physical
input/navigation, Linux completion, OS-requested setup across two boots,
firmware-logo/BGRT restoration, and the actual protected-store codec/comparison
oracles. The targeted comparison mutants must abort; both caller hashes also
remained unchanged. The root personally viewed the live navigated setup and
native "Selecting boot device" captures after lossless PPM-to-PNG conversion.
Other status raster references are HOST rendering evidence, not additional
live-boot captures. The generic manifest's TPM label is descriptive; the actual
recorded device list selects TPM=0, NVMe=1 and xHCI=1, with one CPU.

The actual consumer ROM SHA256 is
`f9262522c93cc9a46996d8ef9e88edd79798a95b2369c1c5e7f168c343d2af01`;
payload ELF SHA256 is
`a13ea3d1925312ea10893c6675ccc6f80bda8fb4140b2ff8c91f6366eec3f4ca`.
Real coreboot cbfstool packaged that ELF and embedded/extracted the exact
resolved CDK2 config, without an FV or seed. The immutable donor ROM remains
`f7a91a9a9b41844aa906e524331877d61f3bd5e3729c2d3e58ca490cc1e8ce7c`,
with matching cbfstool
`d957b383667b07f5b64aeab74d1e3ed8d0c563d97d019c1942ad6f3a03ce380f`.
Its historical qualified coreboot source is `636f67a94129` with previously
recorded codec changes; it is not relabeled a clean producer. The direct
attestation records all 34 source-built image identities and actual ELF layout.

CDK2 HEAD was signed `233a87fcfe3d` during these runs. Production source was
unchanged from the reviewed RAM-window consumer `645393c521cd`; uncommitted
HOST-only scratch-fixture repairs were frozen during native testing and are
separately signed as `522c0a2a7cba`. This is not a clean-checkout or final-head
whole-regression claim. The preceding overlapping run was interrupted and is
preserved separately as NOT a pass.

The additive archive SHA256 is
`d4f69dc0150082b45da34a4cd55d0a6c9cb4df2c4301f4f315756956cda679af`.
It contains the actual completed VM outputs, manifests, generated test media,
raw QMP framebuffer captures, HOST references/oracles and two losslessly
converted live images. Unix sockets and PID files are excluded. The donor has
the existing variable service but no newly installed capsule endpoint: this
suite does NOT prove native E8/RAM CLOSE, a new SMM writer, authenticated newer
firmware installation/restore, disk controller lifecycle, hardware acceptance
or whole-project release completion.
