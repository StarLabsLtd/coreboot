# Actual fresh normal build at ready652

Source: signed clean canonical CDK2 `14384fb9609ae158164dcbcc73126da7e568f1db`.
Producer: signed clean `7ee34bed989c46913c3ee6672fb25e83227c3b6c`.
Actual outer execution: session33652, exit1, aggregate.status1.
Whole-build time: 211.99 seconds; native build: 116.52 seconds, exit0.

Native Core ELF layout, MTRR hostile mutations, LVGL source/config provenance,
UI driver form, framebuffer restore mutant and splash status contract reported
PASS in the actual native build log. Native outer.status is0.

The overall build did not pass. After producer olddefconfig, the selected-tool
capture failed because initial9/build/xcompile did not yet exist. Coreboot
intentionally skips this file for configuration-only Make goals. No producer
firmware compilation, completed ROM admission/seal or guest execution followed.
These are bounded native results, not a new ROM or hardware qualification.

The original recipe, stage, logs and failure remain unchanged. Any source fix
requires review, separate final gates and a new build stage.
