#!/bin/bash
set -euo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C
receipt=/home/sean/tcg2-legacy-boot-digest-host-gates.r087g9
source=/home/sean/Documents/.cdk2-worktrees/tcg2-legacy-boot-digest-after661
origin=/home/sean/tpm-fifo-functional-build-after661
cd "$source"
test ! -e "$receipt/tmp" && test ! -e "$receipt/build"
mkdir "$receipt/tmp" "$receipt/build" "$receipt/build/cdk2"
export TMPDIR=$receipt/tmp
ulimit -c 0
printf '1\n' > "$receipt/aggregate.status"
tools=(bash sh git ssh-keygen gcc perl awk sha256sum cmp cut cat mkdir readlink mktemp rm cp grep sort xargs env)
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
test "$(sha256sum "$receipt/frozen.diff" | cut -d ' ' -f1)" = 144d259c224966451b573607fd3e4fe7499d7ba116f5b573502a86b50279ddb1
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
paths > "$receipt/tool-paths-before.tsv"
sha256sum "$0" "$receipt/README.txt" /usr/bin/time > "$receipt/tools-before.sha256"
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
	46a4a9d6bce99e78ed6fea666b8f281bf35efa30b594983a5ace6a6f70ecd73c "$origin/payload/include/cdk2/config.h" \
	51a81cd1edecaefc0c176d4d3c36bff26b3220c9a95e48d53596cbd1cdf1b1c6 "$origin/payload-resolved.config" \
	3e401544c6a21c95b1d472006b13d3e84196e4a6ffbbdcb1a23d1a5168fc2f7b "$origin/payload-input.config" \
	43023a30a8ffa2a1d407aa77dbfc5065f4258719429bafc63d89388fdfd50465 "$origin/producer-resolved.config" \
	> "$receipt/config-before.sha256"
sha256sum -c "$receipt/config-before.sha256" > "$receipt/config-original-check.log"
grep -qx '#define CONFIG_CDK2_NATIVE_TCG2 1' "$origin/payload/include/cdk2/config.h"
grep -qx '#define CONFIG_CDK2_LINEAR_BOOT 1' "$origin/payload/include/cdk2/config.h"
cp "$origin/payload/include/cdk2/config.h" "$receipt/build/cdk2/config.h"
sha256sum "$receipt/build/cdk2/config.h" >> "$receipt/config-before.sha256"
git show HEAD:src/modules/tcg2/tcg2_measure.c > "$receipt/old-tcg2-measure.c"
sha256sum "$receipt/old-tcg2-measure.c" > "$receipt/old-source-before.sha256"
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
hash_sources=("$source/src/lib/tcg_hash/software_hash.c"
	"$source/src/lib/tcg_hash/vendor/vboot/2sha1.c"
	"$source/src/lib/tcg_hash/vendor/vboot/2sha256.c"
	"$source/src/lib/tcg_hash/vendor/vboot/2sha512.c"
	"$source/src/lib/tcg_hash/vendor/linux/lib/crypto/sm3.c")
includes=(-I"$receipt/build" -I"$source/include"
	-I"$source/src/lib/tcg_hash/vendor/vboot" -I"$source/src/lib/tcg_hash/vendor/linux/include")
common=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1)
models=(transport commands event-log measure service entry diagnostic-coalesce platform-hob software-hash tpm2-acpi-table)
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
	transport) sources=("$source/tests/tcg2_transport_test.c" "$source/src/modules/tcg2/tcg2_transport.c") ;;
	commands) sources=("$source/tests/tcg2_commands_test.c" "$source/src/modules/tcg2/tcg2_commands.c" "$source/src/modules/tcg2/tcg2_transport.c" "${hash_sources[@]}") ;;
	event-log) sources=("$source/tests/tcg2_event_log_test.c" "$source/src/modules/tcg2/tcg2_event_log.c") ;;
	measure) sources=("$source/tests/tcg2_measure_test.c" "$source/src/modules/tcg2/tcg2_measure.c" "$source/src/modules/tcg2/tcg2_event_log.c" "${hash_sources[@]}") ;;
	service) sources=("$source/tests/tcg2_service_test.c" "$source/src/modules/tcg2/tcg2_service.c" "$source/src/modules/tcg2/tcg2_measure.c" "$source/src/modules/tcg2/tcg2_event_log.c" "${hash_sources[@]}") ;;
	entry) sources=("$source/tests/tcg2_entry_test.c" "$source/src/modules/tcg2/tcg2_entry.c") ;;
	diagnostic-coalesce)
		flags+=(-DCDK2_DIAGNOSTIC -DCDK2_TCG2_DIAG_TEST)
		sources=("$source/tests/tcg2_diagnostic_coalesce_test.c" "$source/src/modules/tcg2/diagnostic.c") ;;
	platform-hob)
		flags+=(-Wno-missing-field-initializers -ffunction-sections -fdata-sections -DCDK2_HOST_TEST -Wl,--gc-sections)
		if test "$mode" = asan; then flags+=(--param asan-globals=0); fi
		sources=("$source/tests/tcg2_platform_hob_test.c" "$source/src/modules/tcg2/driver.c" "$source/src/lib/tpm2_acpi_hob.c") ;;
	software-hash) sources=("$source/tests/software_hash_test.c" "${hash_sources[@]}") ;;
	tpm2-acpi-table) sources=("$source/tests/tpm2_acpi_table_test.c" "$source/src/modules/tpm2_acpi_table/tpm2_acpi_table.c" "$source/src/modules/tpm2_acpi_table/driver.c" "$source/src/lib/diagnostic.c") ;;
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
gate hash-provenance "${common[@]}" HOSTCC=/usr/bin/gcc sh tests/software_hash_source_test.sh "$source"
test "$(cat "$receipt/hash-provenance.status")" = 0 || failed=1
for mode in o0 o2; do
	model=measure
	model_inputs
	# Same final test and all other inputs; substitute only the signed old source.
	sources[1]=$receipt/old-tcg2-measure.c
	label=$mode-old-source
	gate "$label-compile" "${common[@]}" /usr/bin/gcc "${flags[@]}" "${sources[@]}" -o "$receipt/build/$label"
	test "$(cat "$receipt/$label-compile.status")" = 0
	gate "$label" "${common[@]}" "$receipt/build/$label"
	opposition=0
	test "$(cat "$receipt/$label.status")" = 1 || opposition=1
	grep -Fq 'variable digest KAT or metadata/data opposition failed' "$receipt/$label.log" || opposition=1
	printf '%s\n' "$opposition" > "$receipt/$label-opposition.status"
	test "$opposition" = 0 || failed=1
done
test "$failed" = 0
