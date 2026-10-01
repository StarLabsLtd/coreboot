# Exact PR453 QEMU archive

`raw.tar.zst` SHA256 is
`986e1d293d6e05e0d5ba043db9c3c2a04855889903a75a2e1314eb4583b962f8`.
Root freshly extracted the completed archive and verified all 98 content
checksums. An opposing worker independently verified the staged 98 checksums
and reviewed `archive-README.md`. This archive records exact source and actual
command outcomes; failures remain qualified rather than replaced.

Source CDK2 `972e68eeabe1e4edcb452cb956f4151a76d4782a` is ready PR453,
directly following PR452. The ROM pairs it with coreboot
`4dd7974f5a5ec4a6e849cb76f0f81d1212645148`. This is a fresh selected native
build and four actual QEMU boots: three setup boots plus the separate BGRT
probe. Setup, Linux runtime-variable and BGRT table/bitmap oracles pass.
The preserved manifests, screenshots, configuration, ELF, ROM and fixture
inputs retain their original identities and paths.

The included coreboot capability checkpatch log is separately at
`138957872743bdf285f7bf39e964386b388f1745`, ready PR350 directly after PR349;
that capability is not part of the compatibility ROM's coreboot revision.
Its ABI and PRIVATE=0/1 producer matrix are component gates, not activated
protected policy. The whole-suite and actual producer join receipts are in
the separately qualified `selecting-status-6d760` follow-up directory.

No production protected-service activation, final project completion,
release-profile or hardware validation is claimed.
