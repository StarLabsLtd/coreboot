PR702 real save and cold reload — 2026-10-05

Signed CDK2 ba629cf33fb4fa99e33989ff557b45877c874ee1 with signed coreboot
producer 7ee34bed989c46913c3ee6672fb25e83227c3b6c, fresh normal Q35 build.
Original closed run:
/home/sean/current693-fwui-qemu.PMINOC/ui-save-reload702
Accepted build:
/home/sean/current693-fwui-qemu.PMINOC/build702-initialized

Archive current702-ui-save-reload.tar.gz contains exactly all 243 regular
files from that closed run. regular-members.list0 gives exact relative
members; original-regular.sha256 gives original absolute paths and hashes.
SHA256 5380c011ded9a97d12abb404ecc77651f625f32fff3399b01930ff4a57d2f05f
Size 66037532 bytes. Four transient QMP/swtpm socket symlinks are excluded;
no regular member is excluded. Coordinator log/status/time and accepted
fresh-build.json are literal separate copies, not recomputed success flags.

Whole coordinator raw 0/failure null, 502.86 seconds. Two real cold guests:
save outer/observer/QEMU 0, 84.121560093 seconds;
reload outer/observer/QEMU 0, 128.680779143 seconds.
Both remain within the unchanged 180-second execution gate. Workflow times
include host admission and observation, and are NOT hardware boot times.

First guest starts from pristine admitted ROM, opens actual setup, edits the
centered checkbox and selects Done. Second starts from only the literal
first guest's saved flash, opens setup with persisted values, Esc returns
to boot and real Linux completes. Actual CBMEM records SUCCESS then ABORTED;
Linux RUNTIME_OK/ACCEPTANCE_OK and S5 are observed in their proper guests.

Real source-derived O0/O2 codecs verify whole 65536-byte SMMSTORE histories:
attrs3/six-byte LvglUiScale, MTC1 -> MTC2 and Linux Probe create/update/delete.
Whole 8MiB ROM exterior and unchanged NVMe are checked. Four causal codec
self-test assertion refusals and byte-exact inverses remain recorded; their
expected Aborted diagnostics are not firmware or sanitizer failures. All
ten recorded codec compiles and actual-M/source/tool/execution ledgers pass.
Closed top/save/reload input maps match before/after; actual union68026
hashes and eight build ledgers pass independent review. Loaded Core and
exact pair/config/media/source identities are bound.

Root and independent reviewer visually inspect actual PPM captures:
initial unchecked/disabled -> checked/enabled with Done focused -> cold
reload checked/enabled. Original and restored STARLABS bitmap pixels match.
Lossless PNG views are outside the immutable run, not substituted for QMP
captures. This does not prove every UI page, mouse, all EFI loaders, TPM/PFP,
every platform or hardware. It closes this normal save/cold-reload workflow.

Earlier PR700 coordinator timeout and reload timeout remain failures.
Initial PR702 uninitialized-LVGL build failed before guest execution; the
separate initialized retry passes. No failed output was overwritten.
Later Root PR703 linked-vendor initialization mistake and Git-config repair
are separate from this closed run; no uncaptured Git-config invariance is
claimed. This archive is the complete regular run snapshot, not bodies of
all 68026 external build/tool inputs; retained signed source and originals
are still needed for full historical reproduction. No hardware action here.
