#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
packet=/home/sean/lvgl620-host621-packet.yyCG8Y/phase-owned-lvgl-and-host-cache-after620-621
ab=/home/sean/splash-status-object-ab.IDrurC
lvgl=/home/sean/phase-owned-lvgl-proof.CuEvQK
repository=/home/sean/Documents/.cdk2-worktrees/splash-status-ephemeral-objects-after620
mkdir -p "$packet"
original_map=$packet/original-copies.tsv
test ! -e "$original_map"
: > "$original_map"

copy_receipt()
{
	local source=$1 relative=$2
	test -f "$source"
	test ! -e "$packet/$relative"
	mkdir -p "$(dirname "$packet/$relative")"
	cp -- "$source" "$packet/$relative"
	cmp "$source" "$packet/$relative"
	printf '%s\t%s\t%s\n' "$(sha256sum "$source" | cut -d ' ' -f 1)" \
		"$relative" "$source" >> "$original_map"
}

for name in run.sh check-ab.py check-ab-first.py check-ab-second.py check-ab.log check-ab.time tools/sh; do
	copy_receipt "$ab/$name" "ab/$name"
done
for variant in baseline candidate; do
	for profile in 0 1; do
		directory=$variant-p$profile
		for name in gate.log gate.time outer.status resolved.config source-head.txt source.patch \
			inputs-before.sha256 inputs-after-check.log source-before.sha256 source-after-check.log \
			include/cdk2/config.h; do
			copy_receipt "$ab/$directory/$name" "ab/$directory/$name"
		done
		while IFS= read -r source; do
			relative=${source#"$ab/"}
			copy_receipt "$source" "ab/$relative"
		done < <(rg --files "$ab/$directory/tmp" | \
			rg '/(argv-[0-9]+-[0-9]+|dependencies\.(make|txt)|inputs\.sha256|config\.h|[^/]+\.(c|log))$' | sort)
	done
done

# Only top-level external proof sources/scripts/raws and explicit selected
# profile logs/configs are copied. No executable, object or baseline source tree.
while IFS= read -r source; do
	relative=${source#"$lvgl/"}
	copy_receipt "$source" "lvgl/$relative"
done < <(rg --files --maxdepth 1 "$lvgl" | rg '\.(sh|c|h|log|time|sha256|txt)$' | sort)
for directory in joined-619 p1 final-p0 closure-p0 closure-p1 compat renderer-without-hotkey baseline-p1; do
	while IFS= read -r source; do
		relative=${source#"$lvgl/"}
		copy_receipt "$source" "lvgl/$relative"
	done < <(rg --files --maxdepth 3 "$lvgl/$directory" | \
		rg '/([^/]+\.(log|time|sha256)|resolved\.config|include/cdk2/config\.h)$' | sort)
done

mkdir -p "$packet/source"
: > "$packet/source/source-blobs.tsv"
paths=(src/boot/Makefile src/modules/dxe_core/entry.c tests/splash_status_contract_test.sh \
	tests/splash_status_report_test.c tests/splash_status_report_test.sh)
for identity in \
	619:1baa1dbbf5050b62a9fa27c2b373af84df9b92ce \
	620:127c80df85986ca77ce50f3bfcb6fc98c3ac41dc \
	621:d6c29549f28cc51d9951db2be965c64c04124ca0; do
	name=${identity%%:*}
	commit=${identity#*:}
	git -C "$repository" archive --format=tar "$commit" "${paths[@]}" \
		-o "$packet/source/source-$name.tar"
	for path in "${paths[@]}"; do
		checksum=$(git -C "$repository" show "$commit:$path" | sha256sum | cut -d ' ' -f 1)
		blob=$(git -C "$repository" rev-parse "$commit:$path")
		printf '%s\t%s\t%s\t%s\t%s\n' "source-$name.tar" "$commit" "$blob" "$checksum" "$path" \
			>> "$packet/source/source-blobs.tsv"
	done
done
printf 'Collected finite public receipts at %s\n' "$packet"
