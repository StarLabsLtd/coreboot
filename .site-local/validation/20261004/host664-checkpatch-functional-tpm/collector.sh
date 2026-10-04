#!/bin/bash
set -euo pipefail
recipe=/home/sean/host664-functional-tpm-collector.wJBQYk
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/host664-checkpatch-functional-tpm
scanner=/home/sean/native-boundary-source-filter-host-gates.VtwT74
host37=/home/sean/host-style37-final.20261004-r1
failed=/home/sean/checkpatch-constant-host-gates.WbHfdg
retry=/home/sean/checkpatch-constant-host-retry.9Mc6Rb
tpmhost=/home/sean/tpm-fifo-command-ready-host-gates.XFCZpH
built=/home/sean/tpm-fifo-functional-build-after661
guests=/home/sean/tpm-fifo-functional-guest-after661
test ! -e "$destination"
for directory in "$scanner" "$host37" "$retry" "$tpmhost"; do
	for name in aggregate execution closure; do
		test "$(cat "$directory/$name.status")" = 0
	done
done
test "$(cat "$scanner/old-source.status")" = 1
test "$(cat "$scanner/old-source-opposition.status")" = 0
test "$(cat "$retry/old-source.status")" = 1
test "$(cat "$retry/old-source-opposition.status")" = 0
test "$(cat "$tpmhost/old-predicate.status")" = 132
test "$(cat "$tpmhost/old-predicate-opposition.status")" = 0
test "$(cat "$failed/driver.status")" = 2
test "$(cat "$failed/execution.status")" = 1
test "$(cat "$failed/aggregate.status")" = 1
test "$(cat "$failed/closure.status")" = 0
for status in "$built"/*.status; do test "$(cat "$status")" = 0; done
test "$(cat "$guests/aggregate.status")" = 1
test "$(cat "$guests/tpm-linux-run.status")" = 0
test "$(cat "$guests/tpm-linux-media.status")" = 0
test "$(cat "$guests/tpm-linux-oracle.status")" = 1
test "$(cat "$guests/tpm-linux-freezer.status")" = 1
jq -e '.name == "tpm-linux" and .qemu_status == 0 and .devices.tpm == 1' \
	"$guests/tpm-linux/manifest.json" >/dev/null
test "$(cat "$guests/tpm-linux/qemu.status")" = 0
grep -Fq 'CDK2_RUNTIME_OK' "$guests/tpm-linux/serial.log"
grep -Fq 'reboot: Power down' "$guests/tpm-linux/serial.log"
! grep -Eq 'TPM_EVENT_LOG_HEX_(BEGIN|END)|TPM_PCR' "$guests/tpm-linux/serial.log"
for prefix in source-tools native-inputs selected-tools; do
	cmp "$built/$prefix-before.json" "$built/$prefix-after.json"
	cmp "$guests/$prefix-before.json" "$guests/$prefix-after.json"
done
cmp "$guests/inputs-before.json" "$guests/inputs-after.json"
sha256sum --quiet -c <<'PINS'
c6e53653d21ad8b6a701d386aace53a3a357d8408b09d1be714dca546071121f  /home/sean/native-boundary-source-filter-host-gates.VtwT74/run.sh
26035128ee171db17fc12bfcd698e563d1aab6fba4587d92f6399f6e97e503eb  /home/sean/native-boundary-source-filter-host-gates.VtwT74/README.txt
10d75d6979759a16bb1bdc0159eda3e79a6ab49e5ee5eca8d2470f3a3e59e272  /home/sean/host-style37-gate-recipe.0Nsv6T/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/host-style37-gate-recipe.0Nsv6T/hash-inputs.py
0e1e0a5c5f57b20632bc4654618d4fe286ef9613918914f3e9294feeca323080  /home/sean/host-style37-gate-recipe.0Nsv6T/README.txt
9d7f043feb9707b42e09de6323f5b1003dff27813f99067c52a86316f6489bd3  /home/sean/checkpatch-constant-host-gates.WbHfdg/run.sh
9503f070c9c146e5f49d4d657b5bff842b5604604926e5368906da579745b9df  /home/sean/checkpatch-constant-host-gates.WbHfdg/README.txt
3d0c0691d698ad331806cdd02699776e2dcab5b3a9734c5a211bd011e12d8d97  /home/sean/checkpatch-constant-host-retry.9Mc6Rb/run.sh
34676f92be4c1a6cc890c5bb92147f37478bd24a8326a86519b30059db24ea8c  /home/sean/checkpatch-constant-host-retry.9Mc6Rb/README.txt
848fbd32ff3ce1dc1edf642564de2b09b226c227e73ec273ed98cd932d0d62d3  /home/sean/tpm-fifo-command-ready-host-gates.XFCZpH/run.sh
05e1ae338b28493d17ccf9e277bd82ce16745023c97f4be7a8c3b581aacbe28e  /home/sean/tpm-fifo-command-ready-host-gates.XFCZpH/cc.sh
69c441959b1a2ec0c503a587f05430820630bfaffa4cdfe1a03edf8af7da77c1  /home/sean/tpm-fifo-command-ready-host-gates.XFCZpH/README.txt
5c3948a00b36293851c9df0a60d40301eee24087f235b2714955d05267ca57fa  /home/sean/tpm-fifo-functional-recipes.4GUdjk/build.sh
b18b1203ebbce80bdb63cfc9fdaa77b8953524b8a7ee57381f0d1c2f6f00c68a  /home/sean/tpm-fifo-functional-recipes.4GUdjk/run-two.sh
35e3eb7d9b7d82b60da90c0548b3196fbb3f42e75249e7fe87c58843b0c4eaa7  /home/sean/tpm-fifo-functional-recipes.4GUdjk/receipt.py
74f6c9e1ad376606b57023112c1267b7bdb254d3cc64d36b6e5321584855bed3  /home/sean/tpm-fifo-functional-recipes.4GUdjk/README.md
PINS
mkdir -p "$destination"
: > "$destination/ORIGINAL_FILES.sha256"
: > "$destination/FILES_MAP.tsv"
copy_file()
{
	local label=$1 path=$2 target
	test -f "$path" && test ! -L "$path"
	target=$destination/$label/${path##*/}
	test ! -e "$target" && test ! -e "$target.gz"
	mkdir -p "$(dirname "$target")"
	sha256sum "$path" >> "$destination/ORIGINAL_FILES.sha256"
	if test "$(stat -c %s "$path")" -gt 1048576; then
		gzip -n -c "$path" > "$target.gz"
		gzip -dc "$target.gz" | cmp "$path" -
		target=$target.gz
	else
		cp -p "$path" "$target"
		cmp "$path" "$target"
	fi
	printf '%s\t%s\n' "$path" "${target#"$destination/"}" >> "$destination/FILES_MAP.tsv"
}
for label in scanner663 host37-664 checkpatch-first-failed checkpatch-retry tpm-functional-host tpm-functional-build tpm-functional-guest; do
	case "$label" in
	scanner663) directory=$scanner ;;
	host37-664) directory=$host37 ;;
	checkpatch-first-failed) directory=$failed ;;
	checkpatch-retry) directory=$retry ;;
	tpm-functional-host) directory=$tpmhost ;;
	tpm-functional-build) directory=$built ;;
	tpm-functional-guest) directory=$guests ;;
	esac
	while IFS= read -r -d '' path; do
		case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff)
			copy_file "$label" "$path" ;;
		esac
	done < <(find "$directory" -maxdepth 1 -type f -print0 | sort -z)
done
copy_file scanner663 "$scanner/run.sh"
copy_file checkpatch-first-failed "$failed/run.sh"
copy_file checkpatch-retry "$retry/run.sh"
copy_file tpm-functional-host "$tpmhost/run.sh"
copy_file tpm-functional-host "$tpmhost/cc.sh"
for name in run.sh hash-inputs.py README.txt; do
	copy_file host37-recipe "/home/sean/host-style37-gate-recipe.0Nsv6T/$name"
done
for name in build.sh run-two.sh receipt.py README.md; do
	copy_file tpm-functional-recipe "/home/sean/tpm-fifo-functional-recipes.4GUdjk/$name"
done
for name in manifest.json serial.log debugcon.log qemu.log qemu.status qemu-command.txt swtpm.log cdk2-config.txt coreboot-config.txt; do
	copy_file tpm-functional-present "$guests/tpm-linux/$name"
done
sha256sum --quiet -c "$destination/ORIGINAL_FILES.sha256"
cp -p "$recipe/collect.sh" "$destination/collector.sh"
cp -p "$recipe/README.md" "$destination/README.md"
cmp "$recipe/collect.sh" "$destination/collector.sh"
cmp "$recipe/README.md" "$destination/README.md"
(
	cd "$destination"
	find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum > ARCHIVE.sha256
	sha256sum --quiet -c ARCHIVE.sha256
)
