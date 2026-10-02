# Quiet consumer rebuild and three-boot acceptance after PR543

The clean signed CDK2 source was
`e22626169c7aa280e9f07ba0dd62545d5ed64b4f` (ready PR543). A fresh independently
owned artifact directory, `/home/sean/cdk2-root-quiet-after543.taP01A`, was
resolved from the genuine quiet protected setup defconfig and immutable older
Q35 producer. Native-image, real LVGL renderer, Core RAM-window and profile
gates returned 0: root session 48913, user unit
`cdk2-root-quiet-consumer-after543-fresh`, invocation
`3ea6ac226f284f3286a3f8db3e27efb9`, runtime 2m27.098s (GNU 2m26.89s),
CPU 2m33.163s, peak memory 191.1M. Actual config/header hashes stayed unchanged.

Real producer cbfstool packaging verified its pinned producer/tool hashes,
direct-image provenance and byte-identical extracted config. The packaged ROM
hash is `c46f75e83f790d62e0a61899c9d12bd39e5a759861ce87edb4f0713ee9e09f1f`;
the payload ELF hash is
`f876be253985a8454a705573067163e2d82ad7dd3b053980ca879d7ed9e35682`.
This uses the older immutable producer with its existing variable service,
not the new capsule provider under development.

Root actually reaped the complete three-boot acceptance replay, session 12392,
with exit 0. Unit `cdk2-root-quiet-threeboot-after543-retry`, invocation
`f3d2788389824cc0b239bb2c076f207c`, ran 1m42.341s (GNU 1m42.31s),
CPU 2m16.303s, peak memory 717.9M. F2 navigation, two-boot OsIndications setup,
restored splash/logo/BGRT, Linux completion and the complete protected-store
oracle passed. The two aborted comparison mutants are expected targeted tests.
Config/header fingerprints passed again after the suite. Root visually viewed
the actual navigated LVGL form and the restored logo with Selecting boot device
text; the PNGs are lossless format conversions of the archived QEMU PPMs.

Two earlier invocations remain failures. The reused older quiet directory
failed its preflight fingerprint check before testing: its config/header no
longer matched its historical receipt. No cause is inferred from timestamps.
The first new three-boot attempt, session 58320, exited 1 because root omitted
the protected-store oracle's required exported COREBOOT_TREE source input.
That harness failure and its VM outputs are retained separately. The corrected
replay uses the exact same packaged ROM and config, with the genuine codec
source input supplied; no firmware code was changed to make it pass.

These are bounded QEMU/HOST consumer results, not a whole regression at PR543,
new native capsule writer admission, hardware validation, or project sign-off.
