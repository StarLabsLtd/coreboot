#!/bin/bash
set -euo pipefail
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/fresh-joined-after654
test ! -e "$destination"
mkdir -p "$destination"
copy_receipt()
{
	local label=$1 source=$2 path=$3 relative target
	test -f "$path" && test ! -L "$path"
	relative=${path#"$source"/}
	target=$destination/$label/$relative
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
	local label=$1 source=$2 depth=$3 path
	while IFS= read -r -d '' path; do
		case "$path" in
			*.log|*.txt|*.time|*.status|*.sha256|*.sh|*.md|*.patch|*.diff|*.list|*.json|*.tsv|*.config|*.png)
				copy_receipt "$label" "$source" "$path" ;;
		esac
	done < <(find "$source" -maxdepth "$depth" -type f -print0 | sort -z)
}
collect build653 /home/sean/fresh-normal-after653.noNGMS 1
collect build653/build /home/sean/fresh-normal-after653.noNGMS/build 1
collect build653/build/initial9 /home/sean/fresh-normal-after653.noNGMS/build/initial9 1
collect build654 /home/sean/fresh-normal-after654.nP6Vda 1
collect build654/build /home/sean/fresh-normal-after654.nP6Vda/build 1
collect build654/build/initial9 /home/sean/fresh-normal-after654.nP6Vda/build/initial9 1
collect build654/ready-host /home/sean/fresh-normal-after654.nP6Vda/ready-host 1
collect receipt-membership-host /home/sean/fresh-receipt-membership-final.rlhoF3 2
collect producer-xcompile-host /home/sean/fresh-producer-xcompile-final.UGloK0 2
collect joined-guests654 /home/sean/fresh-normal-joined-guest-after654.Vr3XYD 12
collect independent-guests654 /home/sean/fresh-normal-guest-independent-review654.o7hehD 2
collect failed-default654 /home/sean/default-regression-after652.hdkK9T 1
cp -p "$0" "$destination/collector.sh"
