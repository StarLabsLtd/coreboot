#!/bin/bash
set -euo pipefail
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/harness-host-after656
recipe=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test ! -e "$destination"
# Preserve the exact reviewed outer recipes, not whichever bytes exist later.
sha256sum --quiet -c <<'PINS'
a59b341e11cd292f914a49a2454b071a0a1b03b18aaf3f74cdad3f98f3d2d683  /home/sean/lvgl-keyboard-focus-host-recipe.SxlWgn/run.sh
827e515738d08dbccc6d1d7064ee595256d58fe875cf03c5604579ee30bbbebb  /home/sean/lvgl-keyboard-focus-host-recipe.SxlWgn/hash-inputs.py
775419185608bf0832b136ee9e706b6236dc87d019e69d6d24b1b29b469a5a9e  /home/sean/lvgl-keyboard-focus-host-recipe.SxlWgn/README.md
b13fcf20577d3d60edbc3d8cd2daf4bd5e4c0785759f0a40bf27e0e32811cfb1  /home/sean/lvgl-keyboard-focus-host-retry-recipe.pMg2oT/run.sh
827e515738d08dbccc6d1d7064ee595256d58fe875cf03c5604579ee30bbbebb  /home/sean/lvgl-keyboard-focus-host-retry-recipe.pMg2oT/hash-inputs.py
19e2819070a811a38a66ec8b9d6b62b94367e4751d941df4d4f82d0e85c3176f  /home/sean/lvgl-keyboard-focus-host-retry-recipe.pMg2oT/README.md
092575d344657d4484a8a5d976c451d429d19ed17ac80187351fc04f025ab20f  /home/sean/tpm-acceptance-recipes.5YqxUa/README.md
a19a526858569fe7be13b271e45164c64b174acb5e07f282fc3cc771b658f39c  /home/sean/tpm-acceptance-recipes.5YqxUa/build.sh
fd59c06a75a62cd55f163cd9bd4d9a4e0d80fcaf04e2cd83b67e72e10a2c70a9  /home/sean/tpm-acceptance-recipes.5YqxUa/run-two.sh
35e3eb7d9b7d82b60da90c0548b3196fbb3f42e75249e7fe87c58843b0c4eaa7  /home/sean/tpm-acceptance-recipes.5YqxUa/receipt.py
PINS
mkdir -p "$destination"
copy_receipt()
{
	local label=$1 source=$2 path=$3 relative target
	test -f "$path" && test ! -L "$path"
	case "$path" in "$source"/*) ;; *) exit 1 ;; esac
	relative=${path#"$source"/}
	target=$destination/$label/$relative
	test ! -e "$target" && test ! -e "$target.gz"
	mkdir -p "$(dirname "$target")"
	sha256sum "$path" >> "$destination/ORIGINAL_FILES.sha256"
	if test "$(stat -c %s "$path")" -gt 1048576; then
		gzip -n -c "$path" > "$target.gz"
		gzip -dc "$target.gz" | cmp "$path" -
	else
		cp -p "$path" "$target"
		cmp "$path" "$target"
	fi
}
collect()
{
	local label=$1 source=$2 path
	test -d "$source" && test ! -L "$source"
	while IFS= read -r -d '' path; do
		case "$path" in
			*.log|*.txt|*.time|*.status|*.sha256|*.sh|*.md|*.patch|*.diff|*.list|*.json|*.tsv|*.config|*.command|*.awk|*.mk)
				copy_receipt "$label" "$source" "$path" ;;
		esac
	done < <(find "$source" -maxdepth 1 -type f -print0 | sort -z)
}
collect legacy-history-first /home/sean/legacy-lvgl-history-gates.d8Z0uK
collect legacy-history-final /home/sean/legacy-lvgl-history-gates-retry.bKSyEC
collect header-coverage-final /home/sean/dxe-header-coverage-gates.BguG71
collect failed-default655 /home/sean/default-regression-after655.JP1abI
collect freezer-final /home/sean/tpm-empty-diagnostics-final.LA8Itf
collect ui-white-failed /home/sean/lvgl-keyboard-focus-host-final.9za0tJ
collect ui-white-failed/run /home/sean/lvgl-keyboard-focus-host-final.9za0tJ/run
collect ui-dark-final /home/sean/lvgl-keyboard-focus-host-retry.tiihsr
collect ui-dark-final/run /home/sean/lvgl-keyboard-focus-host-retry.tiihsr/run
collect ui-original-recipe /home/sean/lvgl-keyboard-focus-host-recipe.SxlWgn
collect ui-retry-recipe /home/sean/lvgl-keyboard-focus-host-retry-recipe.pMg2oT
collect tpm-draft-recipe /home/sean/tpm-acceptance-recipes.5YqxUa
# Only these reviewed source-owned outer helpers are included; no source tree
# or general Python-file traversal is admitted by the receipt filter.
copy_receipt ui-original-recipe /home/sean/lvgl-keyboard-focus-host-recipe.SxlWgn /home/sean/lvgl-keyboard-focus-host-recipe.SxlWgn/hash-inputs.py
copy_receipt ui-retry-recipe /home/sean/lvgl-keyboard-focus-host-retry-recipe.pMg2oT /home/sean/lvgl-keyboard-focus-host-retry-recipe.pMg2oT/hash-inputs.py
copy_receipt tpm-draft-recipe /home/sean/tpm-acceptance-recipes.5YqxUa /home/sean/tpm-acceptance-recipes.5YqxUa/receipt.py
cp -p "$recipe/collect.sh" "$destination/collector.sh"
cmp "$recipe/collect.sh" "$destination/collector.sh"
cp -p "$recipe/README.md" "$destination/README.md"
cmp "$recipe/README.md" "$destination/README.md"
(
	cd "$destination"
	find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum > ARCHIVE.sha256
	sha256sum --quiet -c ARCHIVE.sha256
)
