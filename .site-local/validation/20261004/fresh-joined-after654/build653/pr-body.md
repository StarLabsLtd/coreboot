Generate coreboot's selected-tool file through its own Make output target after
configuration and before binding tools or compiling firmware. Configuration-only
goals intentionally skip xcompile. Record and require the exact fifth invocation,
its actual successful status/time/log and unchanged selected-tool inputs.

Root and a nonauthor worker reviewed all three files before final author gates.
Fresh and historical HOST admission tests pass. One isolated real xcompile-only
Make probe and the same selected-tool helper passed, including actual x86_32
support archives; source, tool, input, configuration and recipe closures matched.
No firmware all, Core, kernel or VM ran in that probe. Root also reran the two
model suites on the exact ready commit.

The earlier ready650 and ready652 failed full-build attempts remain preserved,
with bounded native results distinguished from overall failures. This change is
not a successful new ROM/guest/hardware or full-project claim. Signed linear
successor to PR652, with no configuration or admission bypass.
