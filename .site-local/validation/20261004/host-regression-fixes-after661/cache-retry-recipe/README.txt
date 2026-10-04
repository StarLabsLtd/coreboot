SOURCE-ONLY frozen recipe; do not execute until root releases the HOST compiler lane.

After release, use a NEW absent absolute receipt path (example, not yet allocated):
env -i HOME=/home/sean USER=sean LOGNAME=sean PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin bash /home/sean/capsule-report-cache-host-retry-recipe.I7xlfG/run.sh /home/sean/capsule-report-cache-host-retry.REPLACE_WITH_NEW_UNIQUE_PATH

Bounded HOST only: no producer/Core firmware build, VM, hardware or artifact seed.
Owned source is signed ready658 plus the exact reviewed one-output registration
diff. Producer and mbedtls are clean pinned sources via supported environment
variables; COREBOOT_CONFIG remains empty. The unchanged test creates genuine
standalone defconfig and exercises four targets over n/y/n, rebuilding each
profile, proving unchanged second-build timestamps, auditing every native recipe,
and exercising its existing omission and old-Make refusals. MAKEFLAGS is empty;
Make's default is serial and the separate dry-run audit explicitly uses -j1.

The separate private audit build is newly configured. Actual Make -pn evaluates
the real capsule object recipe. audit.py requires exact generated header,
command-input stamp and both source Makefiles as prerequisites plus the actual
capsule source, with exactly one registered recipe. A private included .mk clears
only this object's .EXTRA_PREREQS; all four bindings must disappear. The third
pass removes the private include and requires all four original bindings again.
No tracked source mutation, skipped ownership checks or invented build rule.

Source/tool/compiler-support/head/diff/recipe closure is captured before/after.
The standalone audit config/header are bound unchanged. Each stage has real argv,
environment, log, time and raw status. execution, closure and aggregate statuses
are separate; initial failure is retained. The full source-owned test's raw status
is native-config-cache.status, and its shell trace preserves n/y/n iterations.
Its existing EXIT trap removes its temporary binaries/configs; this recipe does
not disable that trap or claim retained per-profile object inventory. Audit build
config/database receipts survive. Tracked source and selected support-file hashes
are finite HOST input receipts, not a full system-header or runtime attestation.
Standalone selected targets do not compile LVGL/BearSSL; no vendor population or
submodule initialization is introduced. Unselected gitlinks are recorded by the
source closure, while the explicitly selected mbedtls source is fully hashed.

The first attempt at /home/sean/capsule-report-cache-host-final.20261004-r1
remains immutable: execution1/closure0/aggregate1. It stopped before the full
n/y/n test because the private audit compared single-slash source spelling to
Make's legitimate CDK2_DIR trailing-slash double-slash spelling. The evaluated
row actually contained all four configuration inputs. This new audit retains
the raw printed row and uses lexical os.path.normpath token normalization before
exact equality checks; it adds no substring matching or dependency exemption.
The original recipe and first failed receipts are preserved unchanged. No full
n/y/n pass, cache compiler or source failure is claimed from that first attempt.
