#!/bin/bash
set -euo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C
receipt=/home/sean/private-diagnostic-names-host.PP4WaV
source=/home/sean/Documents/.cdk2-worktrees/private-diagnostic-function-names-after661
origin=/home/sean/fresh-normal-focus-after657.CftZZg/build
cd "$source"
test ! -e "$receipt/tmp" && test ! -e "$receipt/build"
mkdir "$receipt/tmp" "$receipt/build" "$receipt/build/cdk2"
export TMPDIR=$receipt/tmp
ulimit -c 0
printf '1\n' > "$receipt/aggregate.status"
tools=(bash sh git ssh-keygen gcc perl python3 awk sha256sum cmp cut cat mkdir readlink mktemp rm cp grep sort xargs env head tail wc dirname basename uniq)
paths()
{
	local name
	for name in "${tools[@]}"; do
		printf '%s\t%s\n' "$name" "$(readlink -f "$(type -P "$name")")"
	done
}
finish()
{
	local execution=$? closure=0
	trap - EXIT
	set +e
	printf '%s\n' "$execution" > "$receipt/execution.status"
	for kind in source tools config old-source compiler-inputs; do
		sha256sum -c "$receipt/$kind-before.sha256" > "$receipt/$kind-after-check.log" 2>&1 || closure=1
	done
	git rev-parse HEAD > "$receipt/head-after.txt"
	git status --porcelain > "$receipt/status-after.txt"
	git diff --binary > "$receipt/source-after.diff"
	cmp "$receipt/head-before.txt" "$receipt/head-after.txt" || closure=1
	cmp "$receipt/status-before.txt" "$receipt/status-after.txt" || closure=1
	cmp "$receipt/frozen.diff" "$receipt/source-after.diff" || closure=1
	paths > "$receipt/tool-paths-after.tsv"
	cmp "$receipt/tool-paths-before.tsv" "$receipt/tool-paths-after.tsv" || closure=1
	printf '%s\n' "$closure" > "$receipt/closure.status"
	if test "$execution" = 0 && test "$closure" = 0; then
		printf '0\n' > "$receipt/aggregate.status"
		exit 0
	fi
	exit 1
}
trap finish EXIT
git rev-parse HEAD > "$receipt/head-before.txt"
test "$(cat "$receipt/head-before.txt")" = 6faf053c28d7b33de30daeaeae8408340685d006
git verify-commit HEAD > "$receipt/signature.log" 2>&1
git status --porcelain > "$receipt/status-before.txt"
git diff --binary > "$receipt/frozen.diff"
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = 1c8d1c0f2ee6fd704b1b55cb25746d884e93ac8129b5ef927a9b50bb2ebaeef1
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
paths > "$receipt/tool-paths-before.tsv"
sha256sum "$0" "$receipt/README.txt" "$receipt/verify-renames.py" /usr/bin/time > "$receipt/tools-before.sha256"
while IFS=$'\t' read -r name path; do sha256sum "$path"; done < "$receipt/tool-paths-before.tsv" >> "$receipt/tools-before.sha256"
for name in cc1 as ld collect2 lto-wrapper; do
	selected=$(/usr/bin/gcc "-print-prog-name=$name")
	case "$selected" in /*) ;; *) selected=$(type -P "$selected") ;; esac
	sha256sum "$(readlink -f "$selected")" >> "$receipt/tools-before.sha256"
done
for name in libgcc.a liblto_plugin.so libasan.so libubsan.so; do
	sha256sum "$(readlink -f "$(/usr/bin/gcc "-print-file-name=$name")")" >> "$receipt/tools-before.sha256"
done
printf '%s  %s\n' \
	ac668eb9c8a622e020182653fd832e6671741f5e616a01cfa4fb33b8e9b4065f "$origin/include/cdk2/config.h" \
	10f6aef02d7678a1ae8d5ffab8157b5ab0a37438576a645cdf237fe130fa2aac "$origin/resolved.config" \
	4a2ee6d9fba3b30a8be8efbc4392eef077d61b15221e7ade60b1475a8a724258 "$origin/input.config" \
	d2f5db5a8d33dab72cca9587ca958e67c657b1d2ea0bec18ce69bff775bd1492 "$origin/producer-input.config" \
	> "$receipt/config-before.sha256"
sha256sum -c "$receipt/config-before.sha256" > "$receipt/config-original-check.log"
for flag in CDK2_COREBOOT_CAPSULE_PROFILE CDK2_NATIVE_SYSTEM_FMP PAYLOAD_DMA_HANDOFF; do
	grep -qx "#define CONFIG_$flag 1" "$origin/include/cdk2/config.h"
done
cp "$origin/include/cdk2/config.h" "$receipt/build/cdk2/config.h"
sha256sum "$receipt/build/cdk2/config.h" >> "$receipt/config-before.sha256"
mkdir "$receipt/old-source"
for path in src/modules/cpu_arch/model.c src/modules/capsule_runtime/entry.c src/modules/pci_bus/immutable_entry.c; do
	git show "HEAD:$path" > "$receipt/old-source/$(basename "${path%/*}")-$(basename "$path")"
done
sha256sum "$receipt"/old-source/* > "$receipt/old-source-before.sha256"
gate()
{
	local label=$1 status=0
	shift
	printf '%q ' "$@" > "$receipt/$label.command"
	printf '\n' >> "$receipt/$label.command"
	/usr/bin/time -o "$receipt/$label.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
		"$@" > "$receipt/$label.log" 2>&1 || status=$?
	printf '%s\n' "$status" > "$receipt/$label.status"
}
includes=(-I"$receipt/build" -I"$source/include" -I"$source/src/boot")
common=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1)
models=(cpu capsule capsule-entry pci-immutable pci-arena)
modes=(o0 o2 asan ubsan)
model_inputs()
{
	flags=(-std=c11 -Wall -Wextra -Werror -fshort-wchar -g -fno-pie -no-pie "${includes[@]}")
	case "$mode" in
	o0) flags+=(-O0) ;;
	o2) flags+=(-O2) ;;
	asan) flags+=(-O1 -fsanitize=address -fno-sanitize-recover=all) ;;
	ubsan) flags+=(-O1 -fsanitize=undefined -fno-sanitize-recover=all) ;;
	esac
	case "$model" in
	cpu) sources=("$source/tests/cpu_arch_test.c" "$source/src/modules/cpu_arch/model.c") ;;
	capsule) sources=("$source/tests/capsule_runtime_test.c" "$source/src/modules/capsule_runtime/capsule_runtime.c") ;;
	capsule-entry)
		flags+=(-D_GNU_SOURCE -DCDK2_CAPSULE_ENTRY_TEST -DCDK2_DEBUG -DCDK2_DIAGNOSTIC -DCDK2_DIAG_RUNTIME_TRANSITION_TEST)
		sources=("$source/tests/capsule_runtime_entry_test.c" "$source/src/modules/capsule_runtime/entry.c" "$source/src/modules/capsule_runtime/capsule_runtime.c" "$source/src/modules/capsule_runtime/capsule_runtime_abi.c" "$source/src/lib/diagnostic.c") ;;
	pci-immutable) sources=("$source/tests/pci_immutable_entry_test.c" "$source/src/lib/diagnostic.c") ;;
	pci-arena)
		flags+=(-ffunction-sections -fdata-sections -Wl,--gc-sections)
		sources=("$source/tests/pci_dma_arena_test.c" "$source/src/modules/pci_bus/pci_io.c" "$source/src/modules/pci_bus/binding.c" "$source/src/modules/pci_bus/immutable.c") ;;
	esac
}
dependency_logs=()
for mode in "${modes[@]}"; do
	for model in "${models[@]}"; do
		model_inputs
		label=$mode-$model-inputs
		gate "$label" "${common[@]}" /usr/bin/gcc "${flags[@]}" -M -MT model-dependencies "${sources[@]}"
		test "$(cat "$receipt/$label.status")" = 0
		dependency_logs+=("$receipt/$label.log")
	done
done
perl -MText::ParseWords=shellwords -0777 -ne '
	s/\\\n/ /g;
	for (split /\n/) {
		s/^model-dependencies: // or next;
		s/\$\$/\$/g;
		print join("\n", shellwords($_)), "\n";
	}' "${dependency_logs[@]}" | sort -u > "$receipt/compiler-inputs.txt"
test -s "$receipt/compiler-inputs.txt"
xargs -d '\n' sha256sum < "$receipt/compiler-inputs.txt" > "$receipt/compiler-inputs-before.sha256"
failed=0
for mode in "${modes[@]}"; do
	for model in "${models[@]}"; do
		model_inputs
		label=$mode-$model
		gate "$label-compile" "${common[@]}" /usr/bin/gcc "${flags[@]}" "${sources[@]}" -o "$receipt/build/$label"
		if test "$(cat "$receipt/$label-compile.status")" = 0; then
			gate "$label" "${common[@]}" "$receipt/build/$label"
			test "$(cat "$receipt/$label.status")" = 0 || failed=1
		else
			failed=1
		fi
	done
done
gate rename-parity "${common[@]}" python3 "$receipt/verify-renames.py" "$source" "$receipt/old-source"
test "$(cat "$receipt/rename-parity.status")" = 0 || failed=1
gate ledgers "${common[@]}" sh util/check-diagnostic-ledgers.sh \
	migration/reference-diagnostic-sites.tsv migration/diagnostic-ledgers.tsv migration
test "$(cat "$receipt/ledgers.status")" = 0 || failed=1
for specification in cpu_arch:cpu-dxe-diagnostic-parity.tsv:39 capsule_runtime:capsule-runtime-diagnostic-parity.tsv:4 pci_bus:pci-bus-diagnostic-parity.tsv:43; do
	IFS=: read -r module ledger count <<< "$specification"
	gate "parity-$module" "${common[@]}" sh util/check-module-diagnostic-parity.sh \
		"migration/$ledger" "$count" "src/modules/$module"
	test "$(cat "$receipt/parity-$module.status")" = 0 || failed=1
done
test "$failed" = 0
