Native setup save/reload harness HOST gates, 2026-10-05

Source: 346e03bf8246239ce9fa80732aecf610a72529d7, signature G,
parent 3b2489c9ca8d2af24684b0a306040465794eb567; PR700.
Reviewed 13-file binary delta:
04b0b6f666545ab2be937532bf800ea3dd0e2fe7b6d10534c1f901df5dd8851b
Original physical receipts:
/home/sean/current693-fwui-qemu.PMINOC/ui-save-reload-host.fGPOOGGI
This packet contains this README plus all 26 original flat log/status files,
copied byte-for-byte. No compiled artifact, guest image or media is included.

Root executed python3 -B tests/NAME.py for each of:
default_zero_setup_controller_test, normal_fwui_admission_test,
normal_fwui_execution_test, fresh_normal_fwui_admission_test,
normal_fwui_inputs_test. Each log/status pair records the actual process exit.
The first frozen 0eff63dbd46e49aa19028b2e87b34a8a625ec0db6f3b48ca9af9dfa562fb2e20
run had one normal-admission failure: an inherited HOST positive fixture
incorrectly assumed current Kconfig equals the historical observer pin.
Current cf6d and historical 77cd differ because of the earlier USB policy
change. Other initial four suites passed. Failure is retained, not erased.

The final reviewed fixture reads exact retained historical Git blob
6b2547d0b591866cf3aeb148bccf1f34543b87da, verifies its unchanged 77cd pin,
and explicitly refuses the current body in the historical reuse model. No
production admission/pin was loosened. All five complete suites were rerun
after this fix: 51 + 11 + 5 + 10 + 4 = 81 HOST tests passed, raw status 0.
Final logs/statuses use the -final suffix. The signed commit's exact delta
equals the final frozen source used for these gates; diff check also passed.

Additional actual Root commands:
python3 -B util/qemu/bin/run-setup-acceptance-selftest.py
util/lint/cdk2-checkpatch --show-types --file --quiet tests/protected_setup_media_check.c
make lint-stable
All raw exits were 0. The existing hostile setup selftest retained its three
explicit source-binding refusal results. The proper quiet changed-C style
log is empty. All 16 stable checks passed; binary grep notices remain in the
log and are not failures. Independent reviewer fully read all original logs
and statuses and verified signed source, exact delta and these narrow scopes.

This is HOST model/style evidence only. The modeled binder uses explicitly
mocked replay/codec boundaries; these passes do not prove genuine UI writes,
guest execution, sanitizer coverage, full tool/source closure or visual
checked-state semantics. Real source-bound normal firmware, two cold guests,
whole-ROM/store codec opposition and captured-frame review are separate gates.
No whole-project completion or hardware validation claim.
