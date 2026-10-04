## Summary

Add explicit current-source normal firmware-UI build admission without changing
the historical628 default or its production-body reuse guard. Resolve the
original configuration with current Kconfig, build and pack the actual Core,
and bind the source/configuration/tools/native inputs/composition/reference.
Limit opt-in to pristine UI cancellation and Linux-origin setup reset.

Use coreboot's actual selected compiler/subtools/support archive and tracked
vboot inputs. Regenerate the direct composition proof; extract ELF sections
into owned outputs without modifying admitted inputs. Retain the original
180-second guest deadline and prohibit flash seeds, host variable mutators
and host resets. Correct the provenance test's protected-profile count from
the independently defined SMMSTORE/FTW exclusions.

## Review and validation

Root and two independent workers reviewed the complete frozen ten-file change.
Ready commit is signed, clean and has exact reviewed donor bodies on PR649.
Eight required HOST model files, both actual historical saved UI/Linux
oracles, and the actual direct-pair positive/refusal/input-mutation test pass.
Source/tool/recipe and retained artifact identities remain unchanged.

Earlier failed receipts are preserved: an extra inherited framebuffer fixture
omits the capture mode; a protected-profile fixed-count test was incorrect.
The latter is corrected here. The separate readiness fixture repair has
passed in an isolated worktree and will follow separately.

This PR does not claim a new Core build, guest run, hardware validation or
whole-project completion. Those follow at this exact signed ready head.
Final HOST receipts: `/home/sean/cdk2-fresh-admission-final-refreeze.3ek53p`.
