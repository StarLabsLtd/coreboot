Three genuinely unused configuration controls

Baseline: signed CDK2 b6f37a6f095c5e541644cda3412b590d1cce04ce (588).
Candidate: unused-native-config-knobs-after588, only Kconfig/defconfig.
Exactly 25 Kconfig declaration/default/help lines and three defconfig lines
are removed. The full baseline tracked inventory has exactly six occurrences;
the candidate tracked inventory has none. No code, build rule or select
consumer exists. Capsule embedding policy, HPET selection and UART behavior
are intentionally unchanged.

resolve-test.sh genuinely resolves baseline/candidate P0 and P1 configurations.
P0 uses each source's real defconfig. P1 uses the retained resolved profile from
diagnostic-private-formatters-gate.MqmT1a and the actual 9sM2hH producer config.
Unfiltered resolved config/header bytes change; after filtering only the three
retired-symbol lines every remaining byte matches. No unchanged configuration
digest or generic firmware identity is claimed.

object-test.sh compiled eight real boot/Core/CPU/APIC/PE translation units at
O0/O2/Os using each variant's respective generated P0/P1 header. All 48 complete
object pairs match. The first attempt guessed nonexistent src/lib/pe.c and
failed (objects.log); corrected src/boot/pe.c execution is objects-corrected.log,
actual outer 0, GNU elapsed 27.43/user 26.06/sys 1.36. It is representative
native object evidence, not a full-image equality claim.

Normal native-stage, native-coreboot-test and native-direct-image-inventory-test
passed independently in both profiles: p0-stage.log/time actual 0, GNU 23.16;
p1-stage.log/time actual 0, GNU 18.03. Actual readonly pinned BearSSL/LVGL inputs
were supplied by explicit Make arguments. P1 used the actual producer config
and source tree. These are build/HOST tests, not fresh native QEMU boots.

Existing profile-config-test and linear-config-test passed together in
profile-linear-owned-pin.log/time. Initial profile-linear.log and the environment
override retry profile-linear-pinned.log both failed at the nested profile
build's missing bearssl.h; their linear tests passed. Existing := Make ownership
does not honor that environment-only override. The successful run initialized
this WT's genuine clean pinned BearSSL 8ef7680081c61b486622f2d983c0d3d21e83caad
using a readonly local reference. No fixture/guard/Makefile was changed.

source-before.sha256 binds the two frozen candidate source paths; its final
checks passed. baseline is a complete signed-source export. Scripts, raw logs,
normal generated configs/headers and actual objects are retained here.
No new hardware, runtime authority or whole-project validation is inferred.
