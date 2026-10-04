#!/bin/bash
set -euo pipefail
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/native-models-after648
test ! -e "$destination"
mkdir -p "$destination"
copy_group() {
	local label=$1 source=$2 path relative
	while IFS= read -r -d '' path; do
		relative=${path#"$source"/}
		case "$relative" in
			base/*|base-build/*|build/*|default-config/config/*|strict-config/config/*) continue ;;
		esac
		case "$relative" in
			*.log|*.txt|*.time|*.status|*.sha256|*.sh|*.md|*.patch|*.mk|*.list|*.json|*.config|status|*-before|*-after)
				mkdir -p "$destination/$label/$(dirname "$relative")"
				cp -p "$path" "$destination/$label/$relative"
				cmp "$path" "$destination/$label/$relative"
				sha256sum "$path" >> "$destination/ORIGINAL_FILES.sha256"
				;;
		esac
	done < <(find "$source" -type f -print0 | sort -z)
}
copy_group xhci-author /home/sean/Documents/.cdk2-worktrees/xhci-private-model-types/.xhci-gate-recovery.4UFYH6
copy_group xhci-independent /home/sean/xhci-root-independent.Yh4rSb
copy_group runtime-author /home/sean/runtime2-author-gates-configfix.LZnLzl
copy_group runtime-preflight-failed /home/sean/runtime2-author-gates.HUUyQ6
copy_group runtime-config-failed /home/sean/runtime2-author-gates-retry.mIsvTK
copy_group runtime-independent /home/sean/runtime-root-independent.6uKGVO
copy_group checkpatch /home/sean/checkpatch-byte-types-gates.QKhihW
cp -p "$0" "$destination/collector.sh"
find "$destination" -type f | wc -l
du -sh "$destination"
