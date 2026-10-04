#!/usr/bin/env bash
# Source-released exact HOST gates; historical628 artifact is not a new Core.
set -uo pipefail
task_receipt=/home/sean/cdk2-fresh-admission-final-refreeze.3ek53p
task_prior=/home/sean/cdk2-fresh-admission-final-host.41tHIl
task_stage=/home/sean/native-default-zero-hotkey-after628.CfqEvm
task_linux=/home/sean/linux-fwui-epoch-native-after4bb.j6OTbN/lifecycle/same-vm
task_ui=/home/sean/normal-ui-checkbox-native-after969.eAhimP/lifecycle/ui-cancel
cd /home/sean/Documents/.cdk2-worktrees/fresh-normal-fwui-admission-after645 || exit 1
snapshot_source()
{
	git ls-files -s | awk '$1 == "100644" || $1 == "100755" { print substr($0, index($0, "\t") + 1) }' |
	while IFS= read -r task_path; do sha256sum "$task_path" || return; done
}
snapshot_artifacts()
{
	rg --files -uu "$task_stage" "$task_linux" "$task_ui" | LC_ALL=C sort |
	xargs -d '\n' sha256sum
}
snapshot_tools()
{
	for task_tool in bash sh python3 cc gcc g++ x86_64-linux-gnu-gcc x86_64-linux-gnu-g++ objcopy x86_64-linux-gnu-objcopy awk sha256sum realpath cmp sed sort grep git mktemp wc cut tr cp; do
		task_alias=$(command -v "$task_tool") || return
		task_real=$(realpath -e "$task_alias") || return
		printf 'TOOL\t%s\t%s\t%s\n' "$task_tool" "$task_alias" "$task_real"
		sha256sum "$task_real" || return
	done
	for task_subtool in cc1 cc1plus as ld collect2 lto-wrapper; do
		task_selected=$(cc "-print-prog-name=$task_subtool") || return
		case "$task_selected" in /*) ;; *) task_selected=$(command -v "$task_selected") || return ;; esac
		printf 'SELECTED\t%s\t%s\n' "$task_subtool" "$task_selected"
		sha256sum "$(realpath -e "$task_selected")" || return
	done
	for task_archive in libgcc.a liblto_plugin.so; do
		task_selected=$(cc "-print-file-name=$task_archive") || return
		printf 'SUPPORT\t%s\t%s\n' "$task_archive" "$task_selected"
		sha256sum "$(realpath -e "$task_selected")" || return
	done
}
snapshot_source > "$task_receipt/source-before.sha256" || exit 1
snapshot_artifacts > "$task_receipt/artifacts-before.sha256" || exit 1
snapshot_tools > "$task_receipt/tools-before.sha256" || exit 1
git diff --binary | sha256sum > "$task_receipt/freeze-before.sha256"
git status --porcelain=v1 > "$task_receipt/status-before.txt"
sha256sum "$task_receipt/run.sh" > "$task_receipt/recipe-before.sha256"
task_status=0
run_gate()
{
	task_name=$1
	shift
	/usr/bin/time -p -o "$task_receipt/$task_name.time" "$@" > "$task_receipt/$task_name.log" 2>&1
	task_result=$?
	printf '%s=%s\n' "$task_name" "$task_result" | tee -a "$task_receipt/status"
	test "$task_result" = 0 || task_status=1
}
awk '$2 != "tests/direct_pair_provenance_test.sh" { print }' "$task_prior/followup-source-before.sha256" |
	sha256sum -c > "$task_receipt/original-unaffected-source-check.log" 2>&1 || task_status=1
cmp "$task_prior/followup-artifacts-before.sha256" "$task_receipt/artifacts-before.sha256" || task_status=1
cmp "$task_prior/followup-tools-before.sha256" "$task_receipt/tools-before.sha256" || task_status=1
diff -u "$task_prior/followup-source-before.sha256" "$task_receipt/source-before.sha256" > "$task_receipt/approved-test-only-source-delta.diff"
for task_test in fresh_normal_fwui_admission_test normal_fwui_admission_test default_zero_setup_controller_test linux_fwui_reset_test qmp_cbmem_console_test normal_fwui_inputs_test normal_fwui_execution_test linux_fwui_request_test; do
	run_gate "$task_test" python3 -B "tests/$task_test.py"
done
run_gate linux-saved python3 -B util/qemu/bin/assert-default-zero-setup-run.py "$task_linux" default-zero-linux-fw-ui-reset
run_gate ui-saved python3 -B util/qemu/bin/assert-default-zero-setup-run.py "$task_ui" default-zero-control-cancel
run_gate direct_pair_provenance_test bash tests/direct_pair_provenance_test.sh "$PWD/util/qemu/bin/assert-cdk2-direct-provenance.sh" "$task_stage/native/cdk2-coreboot-image.elf" "$task_stage/include/cdk2/config.h" "$task_stage/native/native-direct-images.rsp" "$task_stage/native/native-direct-composition-inventory.tsv" "$task_stage/native/cdk2-direct-image-table" "$task_stage/native/cdk2-elfcheck" "$PWD/tests/direct_composition_contract_test.sh" "$PWD/util/direct-composition-inventory"
run_gate diff-check git diff --check
run_gate shell-syntax bash -n util/qemu/bin/build-normal-fwui-fresh.sh util/qemu/bin/run-one.sh util/qemu/bin/assert-cdk2-direct-provenance.sh tests/direct_pair_provenance_test.sh
run_gate python-syntax python3 -B -c 'import ast, pathlib, sys; [ast.parse(pathlib.Path(p).read_text()) for p in sys.argv[1:]]' tests/fresh_normal_fwui_admission_test.py util/qemu/bin/fresh_normal_fwui.py util/qemu/bin/run-linux-boot-to-fw-ui.py util/qemu/bin/run-normal-boot-to-fw-ui.py util/qemu/bin/assert-default-zero-setup-run.py
snapshot_source > "$task_receipt/source-after.sha256" || task_status=1
snapshot_artifacts > "$task_receipt/artifacts-after.sha256" || task_status=1
snapshot_tools > "$task_receipt/tools-after.sha256" || task_status=1
git diff --binary | sha256sum > "$task_receipt/freeze-after.sha256"
git status --porcelain=v1 > "$task_receipt/status-after.txt"
sha256sum "$task_receipt/run.sh" > "$task_receipt/recipe-after.sha256"
for task_kind in source artifacts tools freeze status recipe; do
	case "$task_kind" in status) task_suffix=txt ;; *) task_suffix=sha256 ;; esac
	cmp "$task_receipt/$task_kind-before.$task_suffix" "$task_receipt/$task_kind-after.$task_suffix" || task_status=1
done
printf '%s\n' "$task_status" > "$task_receipt/aggregate.status"
printf 'FINAL_REFREEZE_HOST_STATUS=%s\n' "$task_status"
exit "$task_status"
