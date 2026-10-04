# Unexecuted bounded LVGL focus host gate

Run only after root and nonauthor source review and exclusive compiler-lane
release. Source is the exact two-file 701418c9 freeze on base signed0bb127.
The normal original input and producer profile are the unchanged actual654
inputs, using the same source-built producer head7ee34. No Core or producer ROM
build, fixture learning, firmware seed, guest, or copied executable is involved.

```sh
env -i PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin TMPDIR=/home/sean LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1 bash /home/sean/lvgl-keyboard-focus-host-recipe.SxlWgn/run.sh NEW_ABSOLUTE_RECEIPTS_PATH
```

The public native-lvgl-renderer-test target builds/runs the real pinned LVGL
renderer/model/smoke/UI fixture, source-builds LvglUiDxe.efi with subsystem11
relocation/layout checks, runs the existing actual framebuffer-restore mutant
and splash-status contract. This recipe does not claim separate sanitizer,
full Core, or QEMU coverage. It preserves first failure and closure receipts.

Compilation uses supported canonical BearSSL/LVGL root overrides. The existing
public provenance test also requires a populated worktree-local pinned LVGL
gitlink path; the recipe populates both empty local vendor paths with detached
linked source worktrees from the matching clean canonical vendor repositories.
It never patches vendors or substitutes symlinks; future source worktree setup
is recorded as an actual stage, and no automatic cleanup is performed.

Sources, both actual/local vendor roots, tools/compiler support, original
configuration inputs, signed owned source heads, approved dirty diff and recipe
are bound before/after. Config resolution is checked against renderer/protected
predicates and compared after the public target. Finite actual native outputs
and inventory are hashed. These are bounded local development receipts, not
complete system-header or hostile-resealing attestation. All files remain
unexecuted until explicit release; TPM recipes remain untouched.
