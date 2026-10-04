Use the existing public `native-lvgl-renderer-test` Make goal for normal
renderer builds. Keep recorded, executed and admitted arguments identical.
This retains normal configuration/header dispatch and runs the existing
renderer, smoke, UI and associated contracts; no bypass or new schema.

The first ready650 build failed at top-level dispatch of the unexported
internal executable target. Its original failed stage is preserved. The next
build uses the canonical checkout's pinned vendors, honoring the existing
vendor-provenance test without copying another source tree or overriding it.

Root and an independent worker reviewed the three-line change. Fresh and
historical HOST models plus actual invocation positive/old-goal/override
refusals pass; source/tool/recipe closures remain unchanged. This is not a
successful new firmware build or guest claim. Signed, linear successor to
PR650 with exact reviewed donor bodies.
