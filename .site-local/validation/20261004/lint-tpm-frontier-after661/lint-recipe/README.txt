SOURCE-ONLY bounded full lint recipe. No execution before root and peer source
release and explicit cheap disjoint HOST lane assignment.

env -i HOME=/home/sean USER=sean LOGNAME=sean PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin bash /home/sean/full-lint-after661-recipe.l4JKYZ/run.sh /home/sean/full-lint-after661-final.REPLACE_WITH_NEW_UNIQUE_PATH

Expected canonical signed6616faf053c28d7b33de30daeaeae8408340685d006. The actual
recipe requires a NEW root-created detached clean source worktree at exactly
that object, with pinned LVGL/BearSSL source gitlinks populated, and hashes actual
tracked regular/symlink/vendor bytes plus canonical before/after. This is source
checkout, not artifact/firmware reuse. The worker recipe performs no shared Git
metadata registration, canonical source/build/media/notes/ready change or push.

Prerequisite WT plan for Root, only after source recipe release:
1. Require /home/sean/Documents/.cdk2-worktrees/full-lint-after661-host absent.
2. git -C /home/sean/Documents/cdk2 worktree add --detach /home/sean/Documents/.cdk2-worktrees/full-lint-after661-host 6faf053c28d7b33de30daeaeae8408340685d006
3. Require canonical LVGL/BearSSL clean and exact selected gitlinks; populate
   empty private 3rdparty/lvgl and 3rdparty/bearssl with detached linked worktrees
   from those canonical vendor repositories, using respectively:
   LVGL85aa60d18b3d5e5588d7b247abf90198f07c8a63 and
   BearSSL8ef7680081c61b486622f2d983c0d3d21e83caad.
4. Verify actual private owned signature/head, vendor heads and all three clean
   statuses. Retain root's real preparation commands/statuses; do not copy any
   existing build or create symlink substitutes. The worker refuses absent,
   wrong-HEAD, dirty or unpopulated private sources, rather than repairing them.

Actual Make targets and write/heavy inspection:
- lint invokes existing allowlist-check (one small C filter O2 compile), full
  stable dispatcher including real Kconfig olddefconfig in owned TMPDIR scratch,
  then full dispatcher license/checkpatch over all selected owned inputs.
- lint-extended invokes the actual extended dispatcher, but current exact HEAD
  has zero lint-extended-* scripts. Inventory/count is saved; raw0 from that
  target is an empty-dispatcher result, NOT an extended assertion/gate closure.
- Separate full-checkpatch invokes the unchanged existing complete file driver,
  which compiles one O2 filter once then checks all C/H/Kconfig inputs under
  Kconfig/src/include/util/tests with the existing fixed source-owned allowlist.
  This is not changed-file, eight-leaf native type ratchet or diff-only lint.
  Existing full lint usage says not expected to pass; any real findings remain
  failure, are logged, and are never baselined, suppressed or fixed by this run.

The actual dispatcher ignores TMPDIR for mktemp .tmpconfig.lintXXXXXX in CWD.
Therefore private exact-HEAD checkout isolates that transient from canonical.
No -J option is used: no tracked-path junit.xml writes. Other scratch products
including Kconfig/compiler filter go into the new owned TMPDIR, and existing
cleanup traps are unchanged. No production firmware/Core/producer compilation,
VM or hardware gate. -j1 serial HOST compilation is only the tiny checkpatch
filter; Kconfig preparation does not produce a firmware image.

Source/tools/selected compiler supports/recipe/head/diff/config closures are
captured before/after with separate raw configure/lint/lint-extended/full-
checkpatch and execution/closure/aggregate statuses. Every requested lint runs
even if an earlier lint returns nonzero. Genuine standalone config/header and
empty producer source/standalone identity are bound. The stable Kconfig script
independently generates and deletes its own scratch config, not that saved one.
Private source/build remains for inspection; no cleanup/reset is automatic.
Receipt hashes establish finite enumerated input closure, not every system
header, shared runtime library, utility or hostile runtime attestation. Full
dispatcher/script inventory and complete file input list disclose actual scope.
